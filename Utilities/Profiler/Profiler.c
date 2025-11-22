
#include "wininclude.h"
#include <Tlhelp32.h>
#include "ProfilerTimerHistory.h"
#include "SimpleUI.h"
#include "SimpleUIMain.h"
#include "SimpleUIPerf.h"
#include "SimpleUIFrame.h"
#include "SimpleUIButton.h"
#include "SimpleUIList.h"
#include "sysutil.h"
#include "utils.h"
#include "assert.h"
#include "winutil.h"
#include "MemoryMonitor.h"
#include "mathutil.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "EArray.h"
#include "iocp.h"
#include "pipeServer.h"
#include "pipeClient.h"
#include "utils.h"
#include "FragmentedBuffer.h"
#include "file.h"
#include "net/net.h"
#include "utilitiesLib.h"
#include "fileutil.h"
#include "SimpleParser.h"
#include "CrypticPorts.h"
#include "gimmeDLLWrapper.h"
#include "sock.h"
#include "TextParser.h"
#include "cmdparse.h"
#include "logging.h"
#include "process_util.h"
#include "mutex.h"

#include "AutoGen/Profiler_c_ast.h"

// TODO:
// - Initial lookup doesn't display "waiting for DNS".
// - IOCP is being starved by MsgWaitForMultipleObjects when not waiting in thread.
// - Make a toggle for showing unhit timers.
// - Make an all-threads "Timer Groups" list.
// - Bug: timerID not matching filter (but was right-clicked), is hidden on new instance create.
// - Buttons to change height of rows.
// - Color code timer groups and instances based on depth/percentage.
// - Save the timer tree open/closed state per-process for duration of profiler.
// - Move flag propagation to decoder.
// - Make separate timer groups for frames and non-frames.
// - Add keyboard input support.
// - Make a countdown breakpoint.
// - Indicate thread's CPU usage from OS.
// - Update moving graph with static mouse position.
// - UI for setting default depth per thread or globally.
// - #define BIT_RANGE2(lo,hi) (((((U32)1 << ((hi) - (lo))) - 1) << ((lo) + 1)) + BIT((lo)))
// - Percent of parent indicator.
// - Reset timers.
// - Column sizers?
// - List of selected threads with time-scalable history graph.
// - Moving items in a list (i.e. the code to support it, not ui for dragging).
// - Hiding/unhiding timers.
// - Per-thread/timer menu items when right-clicked.
// - The awesome "middle-click to close group and move mouse to parent" thing from mmm.
// - Enable/disable child timers, optionally with unlimited depth.
// - Thread snapshot (possibly taken automatically by cleanup thread if taking too long).
// - Some kind of magically keyed "per-item" timers that are optionally enabled and registered.
// - Queued timer UI/data updates so that the window can't partially redraw with new data.
// - Make a loader for the old profiler file format.

// Done:
// - Store the bit position of the last frame so the whole thing doesn't need to be decoded.
// - Create system stats thread to dump process cycles/ticks and other stuff.
// - Add a hotkey for disconnecting a live profile (Ctrl+D maybe).
// - Change percent to be percent-of-frame when frame is locked.
// - Bug: When locking a frame, total frames doesn't change so the average means nothing.
// - Bug: OS values and timer groups don't show locked frame values.
// - Profile-on-connect for new processes.
// - Disable closing the view tab when the process goes away.
// - Reduce large values to top 4 thousands groups.
// - Update window during shell loops (move, resize, menu, etc) or just customize WM_NC*.
// - Bug: Clearing doesn't unlock locked thread frame.
// - Add button to toggle bit/count timers to show accumulated child cycles.
// - Make a command for process record (as opposed to middle click).
// - Calculate total per-frame blocking cycles in decoder.
// - Pause button to ignore timer instance frame updates (or maybe just queue updates).
// - Add a configurable list of recording folders.
// - Add a name filter.
// - Add config file.
// - Add a name filter to TimerID list.
// - Add server name to profiler file name.
// - Better graph color coding (parent, siblings, selected line).
// - Buttons to increase/decrease indent.
// - Send machine/user names to net server/client.
// - Add a toggle button for left list.
// - Network server, so another instance can connect via TCP and view local processes.
// - Hide zero-count timers.
// - Stretch graph to edge.
// - Make way to goto and/or hilite each instance in a timer ID.
// - Don't include cycles in timerID total for instances that are a child of the same timer ID.
// - Free all memory when freeing a playback view.
// - Show the non-CPU timer type (bits, count).
// - Figure out why BitBlt takes so long when it shouldn't be copying anything.
// - Add mouse wheel support.
// - Closed propagation wasn't working correctly.
// - Hitting a breakpoint should send a reader flag update.
// - A way to annotate blocking operations.
// - Button to show full width graph per timer under the main area.
// - Left-click on graph can drag it.  Some way to release the locked position.
// - Right-click on graph locks current position.  Second right-click removes it.
// - Detail view for history graph.
// - Timer ID lists all instances (references to real one, no copied data).
// - Some way to interact with the history graph.
// - Make sure the Timer ID group works with recordings (decoder snapshot bug).
// - Scale graphs horizontally by frame times.
// - Show bytes/bits of data transfer per thread.
// - Split the structs up for various list entry types.
// - Tray icon.
// - Thread for cleaning up exited threads (send unsent frame first).
// - Per-timer history graph.
// - Remaining buffered bytes.
// - Pipe server and client.
// - Processing handles.
// - Pre-wrapped timers in a separate subgroup.
// - Calculate and/or show list depth.
// - Thread names.
// - Process name sent over pipe.
// - Show count of timer ID instances.
// - Unwrapped threads sending a frame after a while (when depth is zero).
// - Hilite the selected line of a list.

//--------------------------------------------------------------------------------------------------

// Debug flags.

#define PRINT_FLAG_TAGS_ON_TIMER_INSTANCE	0
#define PRESSING_T_ADDS_TEST_LLE			0
#define PRESSING_T_SPAWNS_TEST_THREAD		0
#define PRESSING_C_DOES_PROFILER_CONNECT	0
#define PRESSING_K_TEST_THREAD_SAMPLER		0
#define PRINT_FCN_SUCCESS					0
#define VERIFY_ENCODED_FRAMES_AFTER_WRITING	0
#define SEND_BIG_STRING_ON_DISCONNECT		0

//--------------------------------------------------------------------------------------------------

typedef struct TempWindowWD					TempWindowWD;
typedef struct TempWindowList				TempWindowList;
typedef struct TempWindowListEntry			TempWindowListEntry;
typedef struct LeftListEntry				LeftListEntry;
typedef struct TimerHistoryChunk			TimerHistoryChunk;
typedef struct TimerHistoryChunkList		TimerHistoryChunkList;
typedef struct DetailViewWD					DetailViewWD;
typedef struct FolderChangeNotification		FolderChangeNotification;

typedef enum ProfilerConstant {
	BUTTON_ENTIRE_HISTORY_OFFSET			= 17,
	BUTTON_DISABLE_TIMER_OFFSET				= (17 * 2),
	BUTTON_BREAKPOINT_OFFSET				= (17 * 3),
	BUTTON_GO_TO_END_OFFSET					= 17,
	LEFT_LIST_DEFAULT_WIDTH					= 250,

	ARGB_CYCLES_ACTIVE						= 0xffcc4444,
	ARGB_NON_CYCLES							= 0xff44cc44,
	ARGB_HIT_COUNT							= 0xffff00ff,
} ProfilerConstant;

const char* configFileNameCurrent	= "c:\\CrypticSettings\\ProfilerConfig.txt";
const char* configFileNameNext		= "c:\\CrypticSettings\\ProfilerConfig.next.txt";
const char* configFileNamePrevious	= "c:\\CrypticSettings\\ProfilerConfig.prev.txt";
const char* configFileNameOldNext	= "c:\\CrypticSettings\\ProfilerConfig.oldnext.txt";

AUTO_STRUCT;
typedef struct ProfilerConfigServer {
	char*									hostName;
	char*									computerName;
	char*									userName;
	U32										ip;				AST(FORMAT_IP)
	LeftListEntry*							lle;			NO_AST
} ProfilerConfigServer;

AUTO_STRUCT;
typedef struct ProfilerConfigRecordingFolder {
	char*									path;
	LeftListEntry*							lle;			NO_AST
} ProfilerConfigRecordingFolder;

AUTO_STRUCT;
typedef struct ProfilerConfig {
	ProfilerConfigServer**					servers;		AST(NAME(Server))
	ProfilerConfigRecordingFolder**			rfs;			AST(NAME(RecordingFolder))
} ProfilerConfig;

typedef enum TempWindowListEntryType {
	TWLE_GROUP,
	TWLE_THREAD,
	TWLE_SCAN_FRAMES,
	TWLE_OS_GROUP,
	TWLE_OS_CYCLES,
	TWLE_OS_TICKS_USER,
	TWLE_OS_TICKS_KERNEL,
	TWLE_TIMER_INSTANCE,
	TWLE_TIMER_ID_GROUP,
	TWLE_TIMER_ID,
	TWLE_COPY,
} TempWindowListEntryType;

typedef enum TempWindowTextEntryType {
	TWTE_NONE,
	TWTE_ADD_SERVER,
	TWTE_SET_FILTER,
	TWTE_ADD_RECORDING_FOLDER,
} TempWindowTextEntryType;

typedef struct TempWindowListEntryThread {
	U32										threadID;

	AutoTimerDecodedThread*					dt;

	U32										frameCount;
	U32										scanFrameCount;
	U64										scanCycles;

	U32										timerCount;

	U32										byteCountReceived;
	U32										byteCountChunks;

	char*									decoderErrorText;

	TimerHistoryChunkList					history;
	TimerHistoryChunkList					scanHistory;

	struct {
		TempWindowListEntry*				timerIDs;
		TempWindowListEntry*				osGroup;
		TempWindowListEntry*				osCycles;
		TempWindowListEntry*				osTicksUser;
		TempWindowListEntry*				osTicksKernel;
		TempWindowListEntry*				scanFrames;
	} twle;

	struct {
		U64									total;
		U64									active;
		U64									blocking;
	} cycles;
	
	struct {
		U64									cyclesTotal;
		
		struct {
			U64								user;
			U64								kernel;
		} ticksTotal;
	} os;

	struct {
		U32									frameIndex;
		U64									cyclesBegin;
		U64									cyclesDelta;
	} locked;

	struct {
		U32									destroyed			: 1;
		U32									receivedAnUpdate	: 1;
		U32									didSetName			: 1;
		U32									didSetNewFrame		: 1;
		U32									hasVisibleTimerIDs	: 1;
	} flags;
} TempWindowListEntryThread;

typedef struct TempWindowListEntryOS {
	U32										unused;
} TempWindowListEntryOS;

typedef struct TempWindowListEntryTimerIDGroup {
	U32										timerInstanceCount;
} TempWindowListEntryTimerIDGroup;

typedef struct CycleCounts {
	U64										total;
	U64										active;
	U64										blocking;
	U64										other;
} CycleCounts;

typedef struct TempWindowListEntryTimerID {
	U64										timerID;
	U32										timerInstanceCount;
	U32										timerType;

	U32										frameWhenUpdated;

	struct {
		U32									frames;
		U32									hits;
	} count;
	
	CycleCounts								cycles;

	TimerHistoryChunkList					history;

	struct {
		U32									hasRecursion : 1;
	} flags;
} TempWindowListEntryTimerID;

typedef struct TempWindowListEntryTimerInstance {
	TempWindowListEntry*					twleTimerID;
	TempWindowListEntry*					twleCopy;

	U32										instanceID;

	AutoTimerDecodedTimerInstance*			dti;

	struct {
		U32									frames;
		U32									hits;
		U32									subTimers;
	} count;

	CycleCounts								cycles;

	TimerHistoryChunkList					history;

	struct {
		U32									isBlocking				: 1;

		U32									isBreakpoint			: 1;
		U32									closedByDepth			: 1;
		U32									forcedOpen				: 1;
		U32									forcedClosed			: 1;
		U32									parentNotOpen			: 1;
		U32									insideSameTimerID		: 1;
		U32									selectedFromCopy		: 1;

		U32									hiddenByFilter			: 1;
	} flags;
} TempWindowListEntryTimerInstance;

typedef struct TempWindowListEntryCopy {
	TempWindowListEntry*					twleOriginal;

	struct {
		U32									selectedFromOriginal	: 1;
	} flags;
} TempWindowListEntryCopy;

typedef struct TempWindowListEntry {
	TempWindowListEntry*					parent;
	TempWindowListEntry**					children;

	SUIListEntry*							le;
	char*									text;

	U32										childDepth;
	U32										depthFromRoot;

	TempWindowListEntryType					twleType;

	TempWindowListEntry*					twleThread;

	union {
		TempWindowListEntryThread			thread;
		TempWindowListEntryOS				os;
		TempWindowListEntryTimerIDGroup		timerIDGroup;
		TempWindowListEntryTimerID			timerID;
		TempWindowListEntryTimerInstance	timerInstance;
		TempWindowListEntryCopy				copy;
	};

	struct {
		U32									showEntireHistory		: 1;
		U32									childIsUnderMouse		: 1;
		U32									childUnderMouseIsMine	: 1;
		U32									isOpen					: 1;
	} flags;
} TempWindowListEntry;

typedef enum LeftListEntryType {
	LLE_INVALID,
	LLE_TEST,
	LLE_HOST,
	LLE_PROCESS,
	LLE_NET_CLIENTS,
	LLE_NET_CLIENT_PROFILER,
	LLE_NET_CLIENT_PROCESS,
	LLE_NET_CLIENT_TO_PROCESS,
	LLE_GROUP,
	LLE_COMMAND,
	LLE_RECORDINGS_FOLDER,
	LLE_FILE,
} LeftListEntryType;

typedef struct HostLookupThread HostLookupThread;

typedef void (*HostLookupThreadCallback)(	HostLookupThread* hlt,
											void* userPointer,
											const char* hostName,
											U32 ip);

typedef struct LeftListEntryHost {
	char*									hostName;
	U32										hostPort;

	char*									computerName;
	char*									userName;

	U32										ip;

	IOCompletionAssociation*				iocaHostLookup;
	HostLookupThread*						hlt;
	NetLink*								link;

	ProfilerConfigServer*					pcs;
	
	struct {
		U32									hasReceivedData		: 1;
		U32									isLocal				: 1;
		U32									isRemoteProcesses	: 1;
	} flags;
} LeftListEntryHost;

typedef struct LeftListEntryProcess {
	U32										pid;
	U32										pidInstance;
	char*									exeName;

	SUIListEntry*							le;

	TempWindowList*							twl;

	U32										updatesRemaining;
	U32										bytesRemaining;

	U32										startID;
	
	struct {
		U64									cycles;
		U64									ticksUser;
		U64									ticksKernel;
	} osTimes;

	union {
		struct {
			U32								startID;
			PipeClient*						pc;
			FragmentedBuffer*				fb;
			U32								bufferSizeToReceive;
			LeftListEntry**					lleNetClients;
			LeftListEntry**					llesToSendBufferTo;
			char**							stringsToSend;
		} local;
		
		struct {
			LeftListEntry*					lleNetClientProcess;
		} remote;
	};

	ProfileFileWriter*						pfw;

	struct {
		U32									isLocal				: 1;
		U32									timerStarted		: 1;
		U32									threadsAreActive	: 1;
		U32									receivedAnything	: 1;
		U32									gotStartID			: 1;
	} flags;
} LeftListEntryProcess;

typedef struct LeftListEntryNetClient {
	NetLink*								link;

	char*									computerName;
	char*									userName;
	
	LeftListEntry*							lleRemoteProcess;
	
	struct {
		U32									typeCannotChange : 1;
	} flags;
} LeftListEntryNetClient;

typedef struct LeftListEntryNetClientToProcess {
	LeftListEntry*							lleProcess;
	LeftListEntry*							lleClient;
	U32										processStartID;
	U32										clientStartID;
} LeftListEntryNetClientToProcess;

typedef struct LeftListEntryFile {
	char*									fileNameRelative;
} LeftListEntryFile;

typedef struct LeftListEntryRecordingFolder {
	ProfilerConfigRecordingFolder*			rf;
	FolderChangeNotification*				fcn;
	
	struct {
		U32									scanned		: 1;
		U32									notFound	: 1;
	} flags;
} LeftListEntryRecordingFolder;

typedef struct LeftListEntry {
	TempWindowWD*							wdOwner;
	SUIListEntry*							le;

	char*									text;

	LeftListEntry*							parent;
	LeftListEntry**							children;

	LeftListEntryType						lleType;

	union {
		LeftListEntryHost					host;
		LeftListEntryProcess				process;
		LeftListEntryNetClient				netClient;
		LeftListEntryFile					file;
		LeftListEntryNetClientToProcess		netClientToProcess;
		LeftListEntryRecordingFolder		recordingFolder;
	};

	struct {
		U32									destroying : 1;
	} flags;
} LeftListEntry;

typedef struct TempWindowList {
	TempWindowWD*							wdOwner;

	char*									name;

	ProfileFileReader*						pfr;

	AutoTimerReader*						atReader;
	AutoTimerReaderStream*					atReaderStream;
	U32										msStartReading;

	struct {
		U64									cyclesPerSecond;
		U32									countReal;
		U32									countVirtual;
	} cpu;

	AutoTimerDecoder*						atDecoder;

	SUIWindow*								wTabButton;

	SUIWindow*								wFrame;

	SUIWindow*								wList;
	SUIListEntryClass*						lec;
	TempWindowListEntry**					twles;
	TempWindowListEntry*					twleInfo;
	TempWindowListEntry*					twleUnderMouse;

	LeftListEntry*							lleProcess;

	SUIWindow*								wDetailView;

	char*									nameFilter;

	U32										maxDepth;

	U64										cyclesMax;
	U64										cyclesMinReceived;
	U64										cyclesMaxReceived;
	U64										cyclesMaxLocked;
	U64										cyclesViewedDelta;
	F32										scaleCycles;
	F32										scaleCount;

	U64										dragAnchorCycles;

	U64										selectedCyclesBegin;
	U64										selectedCyclesDelta;

	U32										timeWhenInvalidated;
	U32										redrawInterval;

	struct {
		U32									destroyed					: 1;
		U32									isVisible					: 1;
		U32									ignoreUpdates				: 1;
		U32									processIsGone				: 1;
		U32									hasOSCycles					: 1;
	} flags;
} TempWindowList;

typedef enum ShownDataType {
	SHOWN_DATA_CPU_ACTIVE_THEN_BLOCKING,
	SHOWN_DATA_CPU_ACTIVE,
	SHOWN_DATA_CPU_BLOCKING_THEN_ACTIVE,
	SHOWN_DATA_CPU_BLOCKING,
	SHOWN_DATA_HIT_COUNT,
	SHOWN_DATA_COUNT,
} ShownDataType;

typedef struct TempWindowWD {
	SUIWindow*								w;

	SUIWindowProcessingHandle*				phTempForNetComm;

	SUIWindowProcessingHandle*				phIOCompletionsAvailable;

	U32										localReaderCount;
	SUIWindowProcessingHandle*				phLocalReader;
	
	U32										fileReaderCount;
	SUIWindowProcessingHandle*				phFileReader;

	struct {
		U32									msStartTime;
		SUIWindowProcessingHandle*			ph;
		char*								exeName;
	} recordAndExit;

	SUIMainWindow*							mw;

	SUIWindow*								wButtonLeftListToggle;

	SUIWindow*								wButtonIndentInc;
	SUIWindow*								wButtonIndentDec;

	SUIWindow*								wButtonYScaleDec;
	SUIWindow*								wButtonYScaleInc;

	SUIWindow*								wButtonXScaleDec;
	SUIWindow*								wButtonXScaleInc;

	SUIWindow*								wButtonBreakPointToggle;
	SUIWindow*								wButtonUseFrameToggle;
	SUIWindow*								wButtonPauseToggle;
	SUIWindow*								wButtonClear;

	SUIWindow*								wButtonShownData;
	ShownDataType							shownData;

	SUIWindow*								wButtonCyclesOnlyToggle;
	SUIWindow*								wButtonOptions;

	SUIWindow*								wTextEntryPopup;
	TempWindowTextEntryType					textEntryType;

	SUIWindow*								wViewContainer;
	TempWindowList**						twls;

	IOCompletionPort*						iocp;

	PipeServer*								psConnectOnStartup;

	S32										dragSplitterOffsetX;
	
	FragmentedBufferReader*					fbr;

	struct {
		ProfilerConfig						cur;
		ProfilerConfig						saved;
	} config;

	struct {
		SUIWindow*							w;
		SUIListEntryClass*					lec;

		SUIWindowProcessingHandle*			ph;
		S32									width;
		S32									widthTarget;
		S32									widthStart;
		U32									timeStart;

		LeftListEntry*						lleCommands;
		LeftListEntry*						lleHostLocal;
		LeftListEntry*						lleHostRemote;
		LeftListEntry*						lleRecordings;
		LeftListEntry*						lleNetClients;
		LeftListEntry**						lleNetServers;

		struct {
			LeftListEntry*					lleCreateLocal;
			LeftListEntry*					lleCreateThread;
			LeftListEntry*					lleCreateProcess;
			LeftListEntry*					lleAddServer;
			LeftListEntry*					lleSetFilter;
			LeftListEntry*					lleAddRecordingFolder;
			LeftListEntry*					lleToggleRecord;
			LeftListEntry*					lleToggleBreakpoint;
			LeftListEntry*					lleProfileNewProcesses;
			LeftListEntry*					lleConnectToAllServers;
			LeftListEntry*					lleDisconnectAllServers;
			LeftListEntry*					lleCyclePercentages;
		} cmd;
	} leftList;

	struct {
		NetComm*							comm;

		struct {
			NetListen*						listen;
			LeftListEntry**					lleClients;
		} server;
	} net;

	struct {
		U32									iocpWaitingInThread		: 1;

		U32									dragging				: 1;
		U32									mouseOverSplitter		: 1;

		U32									breakPointsEnabled		: 1;

		U32									useFrame				: 1;
		
		U32									showCyclesOnly			: 1;

		U32									profileNewProcesses		: 1;
		
		U32									showTimersWithZeroHits	: 1;
	} flags;
} TempWindowWD;

typedef struct TempWindowCreateParams {
	SUIMainWindow*							mw;
} TempWindowCreateParams;

typedef struct DetailViewWD {
	SUIWindow*								w;
	TempWindowList*							twl;

	SUIWindowProcessingHandle*				ph;
	S32										yTarget;

	U32										selectedFrameIndex;

	U32										msStart;
	S32										yStart;

	ShownDataType							shownData;

	S32										showOtherValue;

	char*									name;
	const TimerHistoryChunkList*			clCycles;
	const TimerHistoryChunkList*			clFrames;
} DetailViewWD;

typedef struct DetailViewCreateParams {
	TempWindowList*							twl;
} DetailViewCreateParams;

typedef struct DrawHistoryGraphData {
	const SUIDrawContext*	dc;
	ShownDataType			shownData;
	S32						x;
	S32						y;
	U32						sy;
	U64						cyclesPerPixelY;
	U64						selectedCyclesBegin;
	U64						selectedCyclesDelta;
	S32						isLocked;

	S32						showOtherValue;
	
	U32						argbCyclesOdd;
	U32						argbCyclesEven;

	struct {
		S32					x0;
		S32					x1;
		U64					cycles;
	} prev;
} DrawHistoryGraphData;

typedef struct FindSelectedHistoryFrameData {
	S32 x;

	struct {
		S32 found;
		U32 frameIndex;
		U64 frameCyclesBegin;
		U64 frameCyclesDelta;
	} out;
} FindSelectedHistoryFrameData;

typedef struct GraphPosition {
	S32 xIndent;
	S32	x;
	S32 y;
	S32 sx;
	S32 sy;
} GraphPosition;

typedef enum FolderChangeNotificationMsgType {
	FCN_MSG_CREATED,
	FCN_MSG_DELETED,
	FCN_MSG_MODIFIED,
	FCN_MSG_LOST_HANDLE,
} FolderChangeNotificationMsgType;

typedef struct FolderChangeNotificationMsg {
	FolderChangeNotificationMsgType		msgType;

	void*								userPointer;
	FolderChangeNotification*			fcn;

	const char*							pathRoot;
	const char*							pathChanged;
} FolderChangeNotificationMsg;

typedef void (*FolderChangeNotificationMsgHandler)(const FolderChangeNotificationMsg* msg);

typedef struct FolderChangeNotification {
	void*								userPointer;
	FolderChangeNotificationMsgHandler	msgHandler;

	char*								path;

	HANDLE								hFolder;
	IOCompletionPort*					iocp;
	IOCompletionAssociation*			ioca;
	OVERLAPPED							ol;
	U8*									olBuffer;
	U32									olBufferSize;
	U32									olBufferSizeUsed;
} FolderChangeNotification;

static struct {
	char		autoLoadProfile[MAX_PATH];
	
	struct {
		U32		pid;
		U32		seconds;
		char	fileName[MAX_PATH];
	} record;
} cmdLine;

AUTO_CMD_STRING(cmdLine.autoLoadProfile, autoLoadProfile) ACMD_CMDLINE;

AUTO_COMMAND ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void recordByPID(	U32 pid,
					U32 seconds,
					const char* fileName)
{
	cmdLine.record.pid = pid;
	cmdLine.record.seconds = seconds;
	strcpy(cmdLine.record.fileName, fileName);
}

static U32 twleCount;
static U32 twleShowPercentages;

//------------ Functions Begin ---------------------------------------------------------------------

#include "../../3rdparty/zlib/zlib.h"

typedef struct SavedCompressHeader {
	U32 sizeThisChunk;
	U32 sizeTotal;
	S32 isFlush;
} SavedCompressHeader;

static void ztest(void){
#if 0 && _M_X64
	#define LOAD_SIZE(x, s) U8* x = (assert(x = fileAlloc("n:\\users\\martin\\zlibcrash\\"#x".martin", s)), x)
	#define LOAD(x) LOAD_SIZE(x, NULL)
	U32 bufferSize;
	LOAD_SIZE(buffer, &bufferSize);
	LOAD(d_buf);
	LOAD(head);
	LOAD(l_buf);
	LOAD(pending_buf);
	LOAD(prev);
	LOAD(sendStart);
	LOAD(state);
	LOAD(window);
	#undef LOAD_SIZE
	#undef LOAD
	z_stream* z = callocStruct(z, z_stream);
	SavedCompressHeader* h;
	U32 outBufferSize = 1024*1024;
	U8* outBuffer = malloc(outBufferSize);
	U32 count = 0;
	U8* randomMemory = malloc(outBufferSize);
	
	srand(time(NULL));
	FOR_BEGIN(i, (S32)outBufferSize);
		randomMemory[i] = 0;//rand() % 255;
	FOR_END;

	deflateInit(z, 4);
	memcpy(z, sendStart, sizeof(*z));
	deflateSetState(z, state, (void*)0x7c7e4c968, d_buf, head, l_buf, pending_buf, prev, window);

	for(h = (SavedCompressHeader*)buffer;
		(U8*)h - buffer + h->sizeThisChunk <= bufferSize &&
			h->sizeThisChunk <= h->sizeTotal;
		h = (SavedCompressHeader*)((U8*)(h + 1) + h->sizeThisChunk))
	{
		printf(	"%3d. 0x%8.8x - 0x%8.8x: chunk %d / %d: ",
				++count,
				(U8*)h - buffer,
				(U8*)h - buffer + h->sizeThisChunk + sizeof(*h),
				h->sizeThisChunk,
				h->sizeTotal);

		z->next_out = outBuffer;
		z->avail_out = outBufferSize;

		if(count == 84){
			FOR_BEGIN(j, (S32)h->sizeThisChunk);
				z->avail_in = 1;
				z->next_in = (U8*)(h + 1) + j;

				if(j + 1 == h->sizeThisChunk){
					S32 g;
					printf("press a key to crash...");
					g = _getch();
					printf("\n");
					
					deflate(z, Z_SYNC_FLUSH);
				}else{
					deflate(z, 0);
				}
			FOR_END;
		}else{
			z->avail_in = h->sizeThisChunk;
			z->next_in = (U8*)(h + 1);
			deflate(z, Z_SYNC_FLUSH);
		}

		printf("%d bytes out\n", outBufferSize - z->avail_out);
	}

	SAFE_FREE(z);
#endif
}

static char* getCommaSeparatedU64(U64 x){
	static int curBuffer = 0;
	// 27+'\0' is the max length of a 64bit value with commas.
	static char bufferArray[10][30]; 
	
	char*	buffer = bufferArray[curBuffer = (curBuffer + 1) % ARRAY_SIZE(bufferArray)];
	S32		digits = 0;
	S32		e = 0;
	
	while(x >= SQR(SQR((U64)1000))){
		x /= 1000;
		e += 3;
	}

	buffer += ARRAY_SIZE(bufferArray[0]) - 1;

	*buffer-- = 0;
	
	if(e){
		do{
			*buffer-- = '0' + (char)(e % 10);

			e = e / 10;
		}while(e);
		
		*buffer-- = 'e';
	}
	
	do{
		*buffer-- = '0' + (char)(x % 10);

		x = x / 10;

		if(x && ++digits == 3){
			digits = 0;
			*buffer-- = ',';
		}
	}while(x);
	
	return buffer + 1;
}

static HANDLE newTempWindowThread(void);

static S32 twMsgHandler(SUIWindow* w,
						TempWindowWD* wd,
						const SUIWindowMsg* msg);

static void fcnIOCompletionMsgHandler(const IOCompletionMsg* msg);

static S32 fcnGetFolderHandle(FolderChangeNotification* fcn){
	if(fcn->hFolder){
		CloseHandle(fcn->hFolder);
		fcn->hFolder = NULL;
	}

	iocpAssociationDestroy(fcn->ioca);
	assert(!fcn->ioca);

	fcn->hFolder = CreateFile(	fcn->path,
								FILE_LIST_DIRECTORY,
								FILE_SHARE_WRITE |
									FILE_SHARE_READ |
									FILE_SHARE_DELETE,
								NULL,
								OPEN_EXISTING,
								FILE_FLAG_BACKUP_SEMANTICS |
									FILE_FLAG_OVERLAPPED,
								NULL);

	if(fcn->hFolder == INVALID_HANDLE_VALUE){
		fcn->hFolder = NULL;
		return 0;
	}
	
	if(!iocpAssociationCreate(	&fcn->ioca,
								fcn->iocp,
								fcn->hFolder,
								fcnIOCompletionMsgHandler,
								fcn))
	{
		assert(0);
	}

	return 1;
}

static void fcnSendMsgLostHandle(FolderChangeNotification* fcn){
	FolderChangeNotificationMsg	msg = {0};
	
	msg.msgType = FCN_MSG_LOST_HANDLE;
	msg.fcn = fcn;
	msg.userPointer = fcn->userPointer;
	msg.pathRoot = fcn->path;
	
	fcn->msgHandler(&msg);
}

static void fcnStartAsyncRead(FolderChangeNotification* fcn){
	PERFINFO_AUTO_START_FUNC();

	FOR_BEGIN(i, 2);
	{
		S32 success = ReadDirectoryChangesW(fcn->hFolder,
											fcn->olBuffer,
											fcn->olBufferSize,
											0,
											FILE_NOTIFY_CHANGE_FILE_NAME |
												FILE_NOTIFY_CHANGE_DIR_NAME |
												FILE_NOTIFY_CHANGE_ATTRIBUTES |
												FILE_NOTIFY_CHANGE_SIZE |
												FILE_NOTIFY_CHANGE_LAST_WRITE |
												FILE_NOTIFY_CHANGE_CREATION |
												FILE_NOTIFY_CHANGE_SECURITY,
											&fcn->olBufferSizeUsed,
											&fcn->ol,
											NULL);

		#if PRINT_FCN_SUCCESS
		{
			if(success){
				printf(	"reading changes on %s\n",
						fcn->path);
			}else{
				printf(	"not reading changes on %s\n",
						fcn->path);
			}
		}
		#endif

		if(success){
			iocpAssociationExpectCompletion(fcn->ioca);
			break;
		}

		// Handle must have gone bad.  See if we can get it back.
		
		Sleep(500);

		if(!fcnGetFolderHandle(fcn)){
			fcnSendMsgLostHandle(fcn);
		}
	}
	FOR_END;

	PERFINFO_AUTO_STOP();
}

static void fcnIOCompletionMsgHandler(const IOCompletionMsg* msg){
	FolderChangeNotification* fcn = msg->userPointer;

	PERFINFO_AUTO_START_FUNC();

	assert(	!fcn ||
			fcn->ioca == msg->ioca);
	
	switch(msg->msgType){
		xcase IOCP_MSG_ASSOCIATION_DESTROYED:{
			if(fcn){
				fcn->ioca = NULL;
			}
		}

		xcase IOCP_MSG_IO_SUCCESS:{
			FILE_NOTIFY_INFORMATION* fni;

			assert(fcn);
			
			if(msg->bytesTransferred){
				for(fni = (void*)fcn->olBuffer;
					fni;
					fni = fni->NextEntryOffset ? (void*)((U8*)fni + fni->NextEntryOffset) : NULL)
				{
					char						pathChanged[1000];
					S32							len = fni->FileNameLength / 2;
					FolderChangeNotificationMsg	msgOut = {0};
					
					msgOut.fcn = fcn;

					MIN1(len, sizeof(pathChanged) - 1);

					FOR_BEGIN(i, len);
						pathChanged[i] = (char)fni->FileName[i];
					FOR_END;

					pathChanged[len] = 0;

					msgOut.userPointer = fcn->userPointer;
					msgOut.pathChanged = pathChanged;
					msgOut.pathRoot = fcn->path;

					switch(fni->Action){
						xcase FILE_ACTION_ADDED:
						acase FILE_ACTION_RENAMED_NEW_NAME:{
							msgOut.msgType = FCN_MSG_CREATED;
						}
						xcase FILE_ACTION_REMOVED:
						acase FILE_ACTION_RENAMED_OLD_NAME:{
							msgOut.msgType = FCN_MSG_DELETED;
						}
						xcase FILE_ACTION_MODIFIED:{
							msgOut.msgType = FCN_MSG_MODIFIED;
						}
						xdefault:{
							assert(0);
						}
					}

					fcn->msgHandler(&msgOut);
				}
			}

			fcnStartAsyncRead(fcn);
		}

		xcase IOCP_MSG_IO_ABORTED:{
			fcnStartAsyncRead(fcn);
		}

		xcase IOCP_MSG_IO_FAILED:{
			// Folder was probably deleted.
			
			assert(fcn);
			
			Sleep(500);
			
			if(!fcnGetFolderHandle(fcn)){
				fcnSendMsgLostHandle(fcn);
			}
		}
		
		xdefault:{
			printf("Unknown iocp error %d.\n", msg->errorCode);
			assert(0);
		}
	}

	PERFINFO_AUTO_STOP();
}

static void fcnDestroy(FolderChangeNotification** fcnInOut){
	FolderChangeNotification* fcn = SAFE_DEREF(fcnInOut);

	if(!fcn){
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	iocpAssociationDestroy(fcn->ioca);

	while(fcn->ioca){
		iocpCheck(fcn->iocp, 10, 10, NULL);
	}

	CloseHandle(fcn->hFolder);
	fcn->hFolder = NULL;

	SAFE_FREE(fcn->path);
	SAFE_FREE(fcn->olBuffer);
	SAFE_FREE(*fcnInOut);

	PERFINFO_AUTO_STOP();
}

static S32 fcnCreate(	FolderChangeNotification** fcnOut,
						const char* path,
						IOCompletionPort* iocp,
						FolderChangeNotificationMsgHandler msgHandler,
						void* userPointer)
{
	FolderChangeNotification* fcn;

	if(	!fcnOut ||
		!path ||
		!iocp ||
		!userPointer ||
		!msgHandler)
	{
		return 0;
	}

	fcn = callocStruct(FolderChangeNotification);

	fcn->path = strdup(path);
	fcn->iocp = iocp;
	
	fcn->userPointer = userPointer;
	fcn->msgHandler = msgHandler;

	fcn->olBufferSize = SQR(1024);
	fcn->olBuffer = malloc(fcn->olBufferSize);

	if(!fcnGetFolderHandle(fcn)){
		fcnDestroy(&fcn);
		return 0;
	}

	fcnStartAsyncRead(fcn);

	*fcnOut = fcn;

	return 1;
}

static void printWithShadow(const SUIDrawContext* dc,
							S32 x,
							S32 y,
							const char* text,
							U32 height,
							U32 argb)
{
	#if 0
	{
		suiPrintText(	dc,
						x + 1,
						y + 1,
						text,
						-1,
						height,
						0xff000000);
	}
	#endif

	suiPrintText(	dc,
					x,
					y,
					text,
					-1,
					height,
					argb);
}

static S32 drawHistoryGraphCallback(const IterateTimerHistoryData* ithd,
									DrawHistoryGraphData* dhgd)
{
	S32 						x0 = ithd->x0;
	S32 						x1 = ithd->x1;
	U32 						y = dhgd->y + dhgd->sy;
	S32 						dx;
	S32 						isSelected;
	S32 						isOddFrame = ithd->frameIndex & 1;
	U64 						cyclesTotal;
	const TimerHistoryFrame*	hf = ithd->hf;
	U64 						cyclesActive;

	switch(dhgd->showOtherValue){
		xcase 1: cyclesActive = hf->cycles.other;
		xcase 2: cyclesActive = hf->cycles.os;
		xcase 3: cyclesActive = hf->osTicks.user;
		xcase 4: cyclesActive = hf->osTicks.kernel;
		xdefault: cyclesActive = hf->cycles.active;
	}

	switch(dhgd->shownData){
		xcase SHOWN_DATA_CPU_ACTIVE_THEN_BLOCKING:
		acase SHOWN_DATA_CPU_BLOCKING_THEN_ACTIVE:{
			cyclesTotal = cyclesActive + hf->cycles.blocking;
		}
		xcase SHOWN_DATA_CPU_ACTIVE:{
			cyclesTotal = cyclesActive;
		}
		xcase SHOWN_DATA_CPU_BLOCKING:{
			cyclesTotal = hf->cycles.blocking;
		}
		xcase SHOWN_DATA_HIT_COUNT:{
			cyclesTotal = hf->count;
		}
		xdefault:{
			assert(0);
		}
	}

	isSelected =	ithd->frameCyclesBegin &&
					ithd->frameCyclesBegin <
						dhgd->selectedCyclesBegin +
						dhgd->selectedCyclesDelta &&
					ithd->frameCyclesBegin +
						ithd->frameCyclesDelta >
						dhgd->selectedCyclesBegin;

	if(dhgd->prev.cycles){
		if(x0 == dhgd->prev.x1){
			if(x0 != x1){
				x0++;
			}
			else if(cyclesTotal < dhgd->prev.cycles &&
					!isSelected)
			{
				return 1;
			}
		}
	}

	dx = x1 - x0 + 1;

	dhgd->prev.cycles = cyclesTotal;
	dhgd->prev.x0 = x0;
	dhgd->prev.x1 = x1;

	if(isSelected){
		suiDrawFilledRect(	dhgd->dc,
							dhgd->x + x0,
							dhgd->y,
							dx,
							dhgd->sy,
							0xff440000);
	}

	FOR_BEGIN(i, 2);
	{
		const U32	partOfPixelMin = 128;
		U64 		cycles;
		U32 		pixels;
		U32 		partOfPixel;
		U32 		argb;
		S32 		skip = 0;
		S32 		isActiveCPU = 0;
		S32			isCount = 0;

		switch(dhgd->shownData){
			xcase SHOWN_DATA_CPU_ACTIVE_THEN_BLOCKING:{
				isActiveCPU = !i;
			}
			xcase SHOWN_DATA_CPU_BLOCKING_THEN_ACTIVE:{
				isActiveCPU = !!i;
			}
			xcase SHOWN_DATA_CPU_ACTIVE:{
				isActiveCPU = 1;
				skip = !!i;
			}
			xcase SHOWN_DATA_CPU_BLOCKING:{
				isActiveCPU = 0;
				skip = !!i;
			}
			xcase SHOWN_DATA_HIT_COUNT:{
				isCount = 1;
				skip = !!i;
			}
			xdefault:{
				assert(0);
			}
		}

		if(skip){
			continue;
		}

		if(isCount){
			cycles = hf->count;

			argb =	isSelected ?
						dhgd->isLocked ?
							0xff55cc44 :
							isOddFrame ?
								0xffffff00 :
								0xffdddd00 :
						isOddFrame ?
							dhgd->argbCyclesOdd :
							dhgd->argbCyclesEven;
		}
		else if(isActiveCPU){
			// Normal CPU.

			cycles = cyclesActive;

			argb =	isSelected ?
						dhgd->isLocked ?
							0xff55cc44 :
							isOddFrame ?
								0xffffff00 :
								0xffdddd00 :
						isOddFrame ?
							dhgd->argbCyclesOdd :
							dhgd->argbCyclesEven;
		}else{
			// Blocking CPU.

			cycles = hf->cycles.blocking;

			argb =	isSelected ?
						isOddFrame ?
							0xff00ffff :
							0xff00dddd :
						isOddFrame ?
							0xff4455cc :
							0xff3a48ae;
		}

		pixels =	cycles /
					dhgd->cyclesPerPixelY;

		partOfPixel =	partOfPixelMin
						+
						(0xff - partOfPixelMin) *
						(cycles % dhgd->cyclesPerPixelY) /
						dhgd->cyclesPerPixelY;

		if(pixels >= y - dhgd->y){
			pixels = y - dhgd->y;
			partOfPixel = 0;
		}

		if(pixels){
			y -= pixels;

			suiDrawFilledRect(	dhgd->dc,
								dhgd->x + x0,
								y,
								dx,
								pixels,
								argb);
		}

		if(partOfPixel > partOfPixelMin){
			U32 argbTop;
			
			argbTop =	0xff000000 |
						((((argb >> 16) & 0xff) * partOfPixel / 0xff) << 16) |
						((((argb >> 8) & 0xff) * partOfPixel / 0xff) << 8) |
						((((argb >> 0) & 0xff) * partOfPixel / 0xff) << 0);

			y--;

			suiDrawFilledRect(	dhgd->dc,
								dhgd->x + x0,
								y,
								dx,
								1,
								argbTop);
		}
	}
	FOR_END;

	return 1;
}

static void drawHistoryGraph(	const SUIDrawContext* dc,
								ShownDataType shownData,
								S32 x,
								S32 y,
								U32 sx,
								U32 sy,
								const TimerHistoryChunkList* clCycles,
								const TimerHistoryChunkList* clFrames,
								U64 cyclesMin,
								U64 cyclesMax,
								U64 cyclesPerPixelY,
								U64 selectedCyclesBegin,
								U64 selectedCyclesDelta,
								S32 isLocked,
								S32 showOtherValue,
								U32 argbCycles,
								U32 argbBackground)
{
	PERFINFO_AUTO_START_FUNC();

	if(!cyclesPerPixelY){
		cyclesPerPixelY = 1;
	}

	PERFINFO_AUTO_START("background", 1);
	{
		suiDrawFilledRect(	dc,
							x,
							y,
							sx,
							sy,
							FIRST_IF_SET(argbBackground, 0xff111122));
	}
	PERFINFO_AUTO_STOP_START("sides", 1);
	{
		suiDrawFilledRect(	dc,
							x - 1,
							y,
							1,
							sy,
							0xff000000);

		suiDrawFilledRect(	dc,
							x + sx,
							y,
							1,
							sy,
							0xff000000);
	}
	PERFINFO_AUTO_STOP_START("graph", 1);
	{
		DrawHistoryGraphData dhgd = {0};

		dhgd.shownData = shownData;
		dhgd.dc = dc;
		dhgd.x = x;
		dhgd.y = y;
		dhgd.sy = sy;
		dhgd.cyclesPerPixelY = cyclesPerPixelY;
		dhgd.selectedCyclesBegin = selectedCyclesBegin;
		dhgd.selectedCyclesDelta = selectedCyclesDelta;
		dhgd.isLocked = isLocked;
		dhgd.showOtherValue = showOtherValue;
		dhgd.argbCyclesOdd = argbCycles;
		dhgd.argbCyclesEven = suiColorInterpAllRGB(0xff, dhgd.argbCyclesOdd, 0, 64);

		timerHistoryIterate(drawHistoryGraphCallback,
							&dhgd,
							clCycles,
							clFrames,
							sx,
							cyclesMin,
							cyclesMax);
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();// FUNC
}

static S32 findSelectedHistoryFrameCallback(const IterateTimerHistoryData* ithd,
											FindSelectedHistoryFrameData* fshfd)
{
	if(	fshfd->x >= ithd->x0 &&
		fshfd->x <= ithd->x1)
	{
		fshfd->out.found = 1;
		fshfd->out.frameIndex = ithd->frameIndex;
		fshfd->out.frameCyclesBegin = ithd->frameCyclesBegin;
		fshfd->out.frameCyclesDelta = ithd->frameCyclesDelta;
		return 0;
	}

	return 1;
}

static S32 findSelectedHistoryFrame(U32* frameIndexOut,
									U64* frameCyclesBeginOut,
									U64* frameCyclesDeltaOut,
									S32 x,
									S32 sx,
									const TimerHistoryChunkList* clCycles,
									const TimerHistoryChunkList* clFrames,
									U64 cyclesMin,
									U64 cyclesMax)
{
	FindSelectedHistoryFrameData fshfd = {0};

	PERFINFO_AUTO_START_FUNC();
	{
		fshfd.x = x;

		timerHistoryIterate(findSelectedHistoryFrameCallback,
							&fshfd,
							clCycles,
							clFrames,
							sx,
							cyclesMin,
							cyclesMax);

		if(fshfd.out.found){
			*frameIndexOut = fshfd.out.frameIndex;
			*frameCyclesBeginOut = fshfd.out.frameCyclesBegin;
			*frameCyclesDeltaOut = fshfd.out.frameCyclesDelta;
		}
	}
	PERFINFO_AUTO_STOP();// FUNC

	return fshfd.out.found;
}

static S32 twleFindTimerInstanceID(	TempWindowListEntry* twle,
									U32 instanceID,
									TempWindowListEntry** twleOut)
{
	if(	twle->twleType == TWLE_TIMER_INSTANCE &&
		twle->timerInstance.instanceID == instanceID)
	{
		*twleOut = twle;
		return 1;
	}

	EARRAY_CONST_FOREACH_BEGIN(twle->children, i, isize);
		if(twleFindTimerInstanceID(	twle->children[i],
									instanceID,
									twleOut))
		{
			return 1;
		}
	EARRAY_FOREACH_END;

	return 0;
}

static void twlePropagateParentNotOpen(	TempWindowListEntry* twle,
										S32 parentNotOpen)
{
	assert(twle->twleType == TWLE_TIMER_INSTANCE);

	twle->timerInstance.flags.parentNotOpen = parentNotOpen;

	if(	twle->timerInstance.flags.forcedOpen
		||
		!twle->timerInstance.flags.closedByDepth &&
		!twle->timerInstance.flags.forcedClosed)
	{
		EARRAY_CONST_FOREACH_BEGIN(twle->children, i, isize);
			twlePropagateParentNotOpen(	twle->children[i],
										parentNotOpen);
		EARRAY_FOREACH_END;
	}
}

static void twlePropagateMaxDepth(	TempWindowListEntry* twle,
									U32 depth,
									S32 parentOpen)
{
	S32 isOpen;

	assert(twle->twleType == TWLE_TIMER_INSTANCE);

	twle->timerInstance.flags.closedByDepth = !depth;

	twle->timerInstance.flags.parentNotOpen = !parentOpen;

	isOpen =	!twle->timerInstance.flags.parentNotOpen &&
				!twle->timerInstance.flags.forcedClosed &&
				(	!twle->timerInstance.flags.closedByDepth ||
					twle->timerInstance.flags.forcedOpen);

	EARRAY_CONST_FOREACH_BEGIN(twle->children, i, isize);
		twlePropagateMaxDepth(	twle->children[i],
								depth ? depth - 1 : 0,
								isOpen);
	EARRAY_FOREACH_END;
}

static void twleCreateTimerIDCopy(	TempWindowListEntry* twle,
									TempWindowList* twl);

static void twleSetShowEntireHistory(	TempWindowListEntry* twle,
										S32 enabled)
{
	twle->flags.showEntireHistory = !!enabled;

	if(twle->flags.showEntireHistory){
		suiListEntrySetHeight(twle->le, 50);
	}else{
		suiListEntrySetHeight(twle->le, 18);
	}
}

static void twleCheckFilter(TempWindowListEntry* twle,
							TempWindowList* twl);

static void twleCreateListEntry(TempWindowListEntry* twle,
								TempWindowList* twl)
{
	if(	twle->parent &&
		!twle->parent->le)
	{
		twleCreateListEntry(twle->parent, twl);
	}

	if(twle->twleType == TWLE_TIMER_INSTANCE){
		if(!twle->timerInstance.twleCopy){
			twleCreateTimerIDCopy(twle, twl);
		}else{
			twleCreateListEntry(twle->timerInstance.twleCopy, twl);
			twleCheckFilter(twle->timerInstance.twleTimerID, twl);
		}
	}
	
	suiListEntryCreate(	&twle->le,
						SAFE_MEMBER(twle->parent, le),
						twl->lec,
						twle);

	suiListEntrySetHeight(twle->le, 18);
	
	if(twle->flags.isOpen){
		suiListEntrySetOpenState(twle->le, 1);
	}
	
	twleSetShowEntireHistory(twle, twle->flags.showEntireHistory);
}

static void twleSetHiddenState(	TempWindowListEntry* twle,
								S32 hidden)
{
	suiListEntrySetHiddenState(SAFE_MEMBER(twle, le), hidden);
}

static void twleCreate(	TempWindowListEntry** twleOut,
						TempWindowListEntryType twleType,
						const char* text,
						TempWindowListEntry* twleParent,
						TempWindowList* twl,
						S32 createListEntry,
						S32 createListEntryHidden)
{
	TempWindowListEntry* twle = callocStruct(TempWindowListEntry);
	
	if(twleOut){
		*twleOut = twle;
	}

	suiCountInc(twleCount);

	twle->twleType = twleType;

	if(text){
		twle->text = strdup(text);
	}

	if(twleParent){
		twle->parent = twleParent;

		eaPush(	&twleParent->children,
				twle);
	}else{
		eaPush(&twl->twles, twle);
	}

	if(createListEntry){
		twleCreateListEntry(twle, twl);
		
		if(createListEntryHidden){
			twleSetHiddenState(twle, 1);
		}
	}
}

static S32 twleGetThread(	TempWindowListEntry* twle,
							TempWindowListEntry** twleThreadOut)
{
	TempWindowListEntry* twleThread = NULL;

	if(twle->twleType == TWLE_COPY){
		twle = twle->copy.twleOriginal;
	}

	if(twle->twleType == TWLE_THREAD){
		twleThread = twle;
	}else{
		twleThread = twle->twleThread;
	}

	*twleThreadOut = twleThread;

	return !!twleThread;
}

static S32 twleGetThreadConst(	const TempWindowListEntry* twle,
								const TempWindowListEntry** twleThreadOut)
{
	const TempWindowListEntry* twleThread = NULL;

	if(twle->twleType == TWLE_COPY){
		twle = twle->copy.twleOriginal;
	}

	if(twle->twleType == TWLE_THREAD){
		twleThread = twle;
	}else{
		twleThread = twle->twleThread;
	}

	*twleThreadOut = twleThread;

	return !!twleThread;
}

static S32 twleGetFramesHistory(const TempWindowListEntry* twle,
								const TimerHistoryChunkList** clOut)
{
	if(twle->twleType == TWLE_COPY){
		twle = twle->copy.twleOriginal;
	}

	if(twle->twleType == TWLE_THREAD){
		*clOut = &twle->thread.history;
	}
	else if(twle->twleType == TWLE_SCAN_FRAMES){
		*clOut = &twle->twleThread->thread.scanHistory;
	}else{
		*clOut = &twle->twleThread->thread.history;
	}

	return !!*clOut;
}

static S32 twleNamePassesListFilter(const TempWindowListEntry* twle,
									const TempWindowList* twl)
{
	char*	s = twl->nameFilter;
	S32		result = 0;
	
	if(!SAFE_DEREF(s)){
		return 1;
	}
	
	while(!result){
		char	delim = 0;
		char*	token = strsep2(&s, "|", &delim);
		
		if(!token){
			break;
		}
		
		if(strstri(twle->text, token)){
			result = 1;
		}
		
		if(delim){
			s[-1] = delim;
		}
	}
	
	return result;
}

static void twleCheckFilter(TempWindowListEntry* twle,
							TempWindowList* twl)
{
	assert(twle->twleType == TWLE_TIMER_ID);

	if(!twleNamePassesListFilter(twle, twl)){
		twleSetHiddenState(twle, 1);
	}else{
		TempWindowListEntry* twleThread;

		if(twleGetThread(twle, &twleThread)){
			twleThread->thread.flags.hasVisibleTimerIDs = 1;
		}
	}
}

static void twleCreateTimerIDCopy(	TempWindowListEntry* twle,
									TempWindowList* twl)
{
	TempWindowListEntry*	twleTimerID = twle->timerInstance.twleTimerID;
	TempWindowListEntry*	twleParentTimerID;
	S32						shouldCheckFilter = 0;

	assert(twle->twleType == TWLE_TIMER_INSTANCE);

	if(	twle->timerInstance.twleCopy ||
		!twleTimerID)
	{
		return;
	}

	if(!twleTimerID->le){
		shouldCheckFilter = 1;
	}else{
		suiListEntryGetHiddenState(twleTimerID->le, &shouldCheckFilter);
	}

	// Find a parent with the same timer ID.

	for(twleParentTimerID = twle->parent;
		twleParentTimerID;
		twleParentTimerID = twleParentTimerID->parent)
	{
		if(twleParentTimerID->twleType != TWLE_TIMER_INSTANCE){
			twleParentTimerID = NULL;
			break;
		}

		if(twleParentTimerID->timerInstance.twleTimerID == twleTimerID){
			twle->timerInstance.flags.insideSameTimerID = 1;
			twleTimerID->timerID.flags.hasRecursion = 1;
			break;
		}
	}

	if(twleParentTimerID){
		twleCreateTimerIDCopy(twleParentTimerID, twl);
	}

	twleCreate(	&twle->timerInstance.twleCopy,
				TWLE_COPY,
				NULL,
				twle->timerInstance.flags.insideSameTimerID ?
					SAFE_MEMBER(twleParentTimerID, timerInstance.twleCopy) :
					twleTimerID,
				twl,
				1,
				0);

	if(shouldCheckFilter){
		twleCheckFilter(twleTimerID, twl);
	}

	twle->timerInstance.twleCopy->copy.twleOriginal = twle;
}

static void twleDestroy(TempWindowListEntry* twle,
						TempWindowList* twl);

static void twAutoTimerDecoderMsgHandler(const AutoTimerDecoderMsg* msg){
	TempWindowList* twl = msg->d.userPointer;
	TempWindowWD*	wd = twl->wdOwner;

	if(	!twl->redrawInterval ||
		twl->redrawInterval > 100)
	{
		twl->redrawInterval = 100;
	}

	switch(msg->msgType){
		xcase AT_DECODER_MSG_SYSTEM_INFO:{
			char buffer[100];
			
			twl->cpu.cyclesPerSecond = msg->systemInfo.cpu.cyclesPerSecond;
			twl->cpu.countReal = msg->systemInfo.cpu.countReal;
			twl->cpu.countVirtual = msg->systemInfo.cpu.countVirtual;

			twleSetHiddenState(twl->twleInfo, 0);
			
			sprintf(buffer,
					"CPU Cycles Per Second: %s",
					getCommaSeparatedU64(twl->cpu.cyclesPerSecond));
			twleCreate(NULL, TWLE_GROUP, buffer, twl->twleInfo, twl, 1, 0);
			
			sprintf(buffer,
					"CPU Count (Real): %s",
					getCommaSeparatedU64(twl->cpu.countReal));
			twleCreate(NULL, TWLE_GROUP, buffer, twl->twleInfo, twl, 1, 0);

			sprintf(buffer,
					"CPU Count (Virtual): %s",
					getCommaSeparatedU64(twl->cpu.countVirtual));
			twleCreate(NULL, TWLE_GROUP, buffer, twl->twleInfo, twl, 1, 0);
		}

		xcase AT_DECODER_MSG_DT_CREATED:{
			TempWindowListEntry* twle;
			TempWindowListEntry* twleTimerIDs;
			TempWindowListEntry* twleOSGroup;

			// Create thread entry.

			twleCreate(	&twle,
						TWLE_THREAD,
						NULL,
						NULL,
						twl,
						1,
						0);

			twle->thread.dt = msg->dt.dt;
			twle->thread.threadID = msg->dtCreated.id;
			
			// Set flags on history.
			
			twle->thread.history.flags.hasCyclesBegin = 1;
			twle->thread.history.flags.hasCyclesActive = 1;
			twle->thread.history.flags.hasCyclesBlocking = 1;
			
			twle->thread.history.flags.hasOSCycles = 1;
			twle->thread.history.flags.hasOSTicksUser = 1;
			twle->thread.history.flags.hasOSTicksKernel = 1;
			
			// Set flags on scanHistory.

			twle->thread.scanHistory.flags.hasCyclesBegin = 1;
			twle->thread.scanHistory.flags.hasCyclesActive = 1;
			
			twle->thread.scanHistory.flags.hasOSCycles = 1;
			twle->thread.scanHistory.flags.hasOSTicksUser = 1;
			twle->thread.scanHistory.flags.hasOSTicksKernel = 1;

			twle->text = strdupf(	"%d: Unknown%s",
									twle->thread.threadID,
									twl->atReader &&
										twle->thread.threadID == GetCurrentThreadId() ?
											" (me)" :
											"");

			autoTimerDecodedThreadSetUserPointer(msg->dt.dt, twle);

			// Create thread's timerIDGroup entry.

			twleCreate(	&twleTimerIDs,
						TWLE_TIMER_ID_GROUP,
						"Timer Groups",
						twle,
						twl,
						1,
						1);

			twle->thread.twle.timerIDs = twleTimerIDs;

			twleTimerIDs->twleThread = twle;
			
			// Create the os group.
			
			twleCreate(&twle->thread.twle.osGroup, TWLE_OS_GROUP, "OS Values", twle, twl, 1, 1);
			twle->thread.twle.osGroup->twleThread = twle;

			twleOSGroup = twle->thread.twle.osGroup;

			twleCreate(&twle->thread.twle.osCycles, TWLE_OS_CYCLES, "Cycles", twleOSGroup, twl, 1, 1);
			twle->thread.twle.osCycles->twleThread = twle;

			twleCreate(&twle->thread.twle.osTicksUser, TWLE_OS_TICKS_USER, "Ticks:User", twleOSGroup, twl, 1, 1);
			twle->thread.twle.osTicksUser->twleThread = twle;

			twleCreate(&twle->thread.twle.osTicksKernel, TWLE_OS_TICKS_KERNEL, "Ticks:Kernel", twleOSGroup, twl, 1, 1);
			twle->thread.twle.osTicksKernel->twleThread = twle;
		}

		xcase AT_DECODER_MSG_DT_DESTROYED:{
			TempWindowListEntry* twle = msg->dt.userPointer;

			if(twle){
				assert(twle->twleType == TWLE_THREAD);

				twle->thread.flags.destroyed = 1;
			}
		}
		
		xcase AT_DECODER_MSG_DT_ERROR:{
			TempWindowListEntry* twle = msg->dt.userPointer;

			if(twle){
				assert(twle->twleType == TWLE_THREAD);

				estrPrintf(	&twle->thread.decoderErrorText,
							"%s (frame %d)",
							FIRST_IF_SET(msg->dtError.errorText, "unknown"),
							twle->thread.frameCount);
			}
		}

		xcase AT_DECODER_MSG_DT_NAMED:{
			TempWindowListEntry* twle = msg->dt.userPointer;

			if(twle){
				twle->thread.flags.didSetName = 1;

				SAFE_FREE(twle->text);

				twle->text = strdupf(	"%d: %s%s",
										twle->thread.threadID,
										msg->dtNamed.name,
										twl->atReader &&
											twle->thread.threadID == GetCurrentThreadId() ?
												" (me)" :
												"");
			}
		}

		xcase AT_DECODER_MSG_DT_FRAME_UPDATE:{
			TempWindowListEntry* twle = msg->dt.userPointer;

			if(twle){
				if(FALSE_THEN_SET(twle->thread.flags.receivedAnUpdate)){
					twleSetHiddenState(twle->thread.twle.timerIDs, 0);
				}

				twle->thread.byteCountReceived += msg->dtFrameUpdate.bytesReceived;

				if(!twl->flags.ignoreUpdates){
					U64 cyclesBegin = msg->dtFrameUpdate.cycles.begin;
					U64 cyclesEnd = cyclesBegin +
									msg->dtFrameUpdate.cycles.active;

					if(	!twl->cyclesMinReceived ||
						cyclesBegin < twl->cyclesMinReceived)
					{
						twl->cyclesMinReceived = cyclesBegin;
					}

					MAX1(twl->cyclesMaxReceived, cyclesEnd);

					if(!twl->cyclesMaxLocked){
						twl->cyclesMax = twl->cyclesMaxReceived;
					}

					if(!TRUE_THEN_RESET(twle->thread.flags.didSetNewFrame)){
						twle->thread.frameCount++;
					}

					twle->thread.cycles.total +=	msg->dtFrameUpdate.cycles.active +
													msg->dtFrameUpdate.cycles.blocking;

					twle->thread.cycles.active += msg->dtFrameUpdate.cycles.active;
					twle->thread.cycles.blocking += msg->dtFrameUpdate.cycles.blocking;
					
					twle->thread.os.cyclesTotal += msg->dtFrameUpdate.os.cycles;
					twle->thread.os.ticksTotal.user += msg->dtFrameUpdate.os.ticks.user;
					twle->thread.os.ticksTotal.kernel += msg->dtFrameUpdate.os.ticks.kernel;
					
					if(msg->dtFrameUpdate.os.cycles){
						twl->flags.hasOSCycles = 1;

						twleSetHiddenState(twle->thread.twle.osGroup, 0);
						twleSetHiddenState(twle->thread.twle.osCycles, 0);
					}

					if(msg->dtFrameUpdate.os.ticks.user){
						twleSetHiddenState(twle->thread.twle.osGroup, 0);
						twleSetHiddenState(twle->thread.twle.osTicksUser, 0);
					}

					if(msg->dtFrameUpdate.os.ticks.kernel){
						twleSetHiddenState(twle->thread.twle.osGroup, 0);
						twleSetHiddenState(twle->thread.twle.osTicksKernel, 0);
					}

					timerHistoryFrameAddValues(	&twle->thread.history,
												twle->thread.frameCount,
												1,
												msg->dtFrameUpdate.cycles.active,
												msg->dtFrameUpdate.cycles.blocking,
												msg->dtFrameUpdate.cycles.begin,
												0,
												msg->dtFrameUpdate.os.cycles,
												msg->dtFrameUpdate.os.ticks.user,
												msg->dtFrameUpdate.os.ticks.kernel);
				}
			}
		}

		xcase AT_DECODER_MSG_DT_SCAN_FRAME:{
			TempWindowListEntry* twle = msg->dt.userPointer;

			if(!twl->flags.ignoreUpdates){
				twle->thread.scanFrameCount++;
				twle->thread.scanCycles += FIRST_IF_SET(msg->dtScanFrame.os.cycles,
														msg->dtScanFrame.cycles.total);

				if(	!twl->cyclesMinReceived ||
					msg->dtScanFrame.cycles.begin < twl->cyclesMinReceived)
				{
					twl->cyclesMinReceived = msg->dtScanFrame.cycles.begin;
				}

				MAX1(	twl->cyclesMaxReceived,
						msg->dtScanFrame.cycles.begin + msg->dtScanFrame.cycles.total);

				if(!twl->cyclesMaxLocked){
					twl->cyclesMax = twl->cyclesMaxReceived;
				}

				timerHistoryFrameAddValues(	&twle->thread.scanHistory,
											twle->thread.scanFrameCount,
											1,
											msg->dtScanFrame.cycles.total,
											0,
											msg->dtScanFrame.cycles.begin,
											0,
											msg->dtScanFrame.os.cycles,
											msg->dtScanFrame.os.ticks.user,
											msg->dtScanFrame.os.ticks.kernel);
				
				if(!twle->thread.twle.scanFrames){
					twleCreate(	&twle->thread.twle.scanFrames,
								TWLE_SCAN_FRAMES,
								"Scan Frames",
								twle,
								twl,
								1,
								0);

					twle->thread.twle.scanFrames->twleThread = twle;
				}else{
					twleSetHiddenState(twle->thread.twle.scanFrames, 0);
				}
			}
		}

		xcase AT_DECODER_MSG_DT_MAX_DEPTH_UPDATE:{
			TempWindowListEntry* twle = msg->dt.userPointer;

			twl->maxDepth = msg->dtMaxDepthUpdate.maxDepth;

			if(twle){
				EARRAY_CONST_FOREACH_BEGIN(twle->children, i, isize);
					TempWindowListEntry* twleChild = twle->children[i];

					if(twleChild->twleType == TWLE_TIMER_INSTANCE){
						twlePropagateMaxDepth(	twleChild,
												msg->dtMaxDepthUpdate.maxDepth,
												1);
					}
				EARRAY_FOREACH_END;
			}
		}

		xcase AT_DECODER_MSG_DTI_CREATED:{
			AutoTimerDecodedTimerInstance* dtiParent;

			if(autoTimerDecodedTimerInstanceGetParent(msg->dti.dti, &dtiParent)){
				TempWindowListEntry* twle;
				TempWindowListEntry* twleParent;
				TempWindowListEntry* twleThread = msg->dt.userPointer;

				if(dtiParent){
					// Get the parent timer.

					autoTimerDecodedTimerInstanceGetUserPointer(dtiParent, &twleParent);
				}else{
					// The parent is the thread.

					twleParent = twleThread;
				}

				if(twleParent){
					twleCreate(	&twle,
								TWLE_TIMER_INSTANCE,
								msg->dtiCreated.name,
								twleParent,
								twl,
								0,
								0);
					
					twle->timerInstance.history.flags.hasCount = 1;
					twle->timerInstance.history.flags.hasCyclesActive = 1;
					twle->timerInstance.history.flags.hasCyclesBlocking = 1;
					
					if(msg->dtiCreated.infoType != PERFINFO_TYPE_CPU){
						twle->timerInstance.history.flags.hasCyclesOther = 1;
					}

					twle->timerInstance.flags.isBlocking = !!msg->dtiCreated.flags.isBlocking;

					twle->twleThread = twleThread;
					twleThread->thread.timerCount++;

					if(	msg->dtiCreated.timerID &&
						twleParent->twleType == TWLE_TIMER_INSTANCE &&
						FALSE_THEN_SET(twleThread->thread.flags.didSetName))
					{
						SAFE_FREE(twleThread->text);

						twleThread->text = strdupf(	"%d: firstTimer=%s%s",
													twleThread->thread.threadID,
													msg->dtiCreated.name,
													twl->atReader &&
														twleThread->thread.threadID == GetCurrentThreadId() ?
															" (me)" :
															"");
					}

					twle->timerInstance.dti = msg->dti.dti;
					twle->timerInstance.instanceID = msg->dtiCreated.instanceID;

					if(!msg->dtiCreated.timerID){
						twle->timerInstance.flags.forcedOpen = 1;
					}

					if(twleParent->twleType == TWLE_TIMER_INSTANCE){
						twle->depthFromRoot = twleParent->depthFromRoot + 1;

						if(	twleParent->timerInstance.flags.parentNotOpen
							||
							twleParent->timerInstance.flags.closedByDepth &&
							!twleParent->timerInstance.flags.forcedOpen
							||
							twleParent->timerInstance.flags.forcedClosed)
						{
							twle->timerInstance.flags.parentNotOpen = 1;
						}
					}

					// Check if this timer is closed by depth.

					if(	!twle->timerInstance.flags.forcedOpen &&
						twle->depthFromRoot >= twl->maxDepth)
					{
						twle->timerInstance.flags.closedByDepth = 1;
					}

					// Increase depth on all parents if this is the first child.

					if(eaSize(&twleParent->children) == 1){
						U32						childDepth;
						TempWindowListEntry*	twleAncestor;

						for(twleAncestor = twleParent, childDepth = 1;
							twleAncestor && childDepth > twleAncestor->childDepth;
							twleAncestor = twleAncestor->parent, childDepth++)
						{
							twleAncestor->childDepth = childDepth;
						}
					}

					// Increase subtimer count on parents.

					{
						TempWindowListEntry* twleCur = twle->parent;

						while(	twleCur &&
								twleCur->twleType == TWLE_TIMER_INSTANCE)
						{
							twleCur->timerInstance.count.subTimers++;
							twleCur = twleCur->parent;
						}
					}

					autoTimerDecodedTimerInstanceSetUserPointer(twle->timerInstance.dti,
																twle);

					// Create entry in timer ID group.

					if(	twleThread &&
						msg->dtiCreated.timerID)
					{
						TempWindowListEntry* twleTimerIDs = twleThread->thread.twle.timerIDs;
						TempWindowListEntry* twleTimerID = NULL;

						assert(twleTimerIDs);

						EARRAY_CONST_FOREACH_BEGIN(twleTimerIDs->children, i, isize);
							TempWindowListEntry* twleCur = twleTimerIDs->children[i];

							assert(twleCur->twleType == TWLE_TIMER_ID);

							if(	twleCur->timerID.timerID == msg->dtiCreated.timerID &&
								twleCur->timerID.timerType == msg->dtiCreated.infoType)
							{
								twleTimerID = twleCur;
								break;
							}
						EARRAY_FOREACH_END;

						if(!twleTimerID){
							twleCreate(	&twleTimerID,
										TWLE_TIMER_ID,
										msg->dtiCreated.name,
										twleTimerIDs,
										twl,
										0,
										0);
							
							twleTimerID->timerID.history.flags = twle->timerInstance.history.flags;

							twleTimerIDs->timerIDGroup.timerInstanceCount++;

							twleTimerID->twleThread = twleThread;
							twleTimerID->timerID.timerID = msg->dtiCreated.timerID;
							twleTimerID->timerID.timerType = msg->dtiCreated.infoType;
						}

						twle->timerInstance.twleTimerID = twleTimerID;
					}
				}

				if(wd->flags.showTimersWithZeroHits){
					twleCreateListEntry(twle, twl);
				}
			}
		}

		xcase AT_DECODER_MSG_DTI_DESTROYED:{
			TempWindowListEntry* twle = msg->dti.userPointer;

			if(twle){
				assert(twle->twleType == TWLE_TIMER_INSTANCE);

				twle->timerInstance.dti = NULL;
				
				twleDestroy(twle, twl);
			}
		}

		xcase AT_DECODER_MSG_DTI_FRAME_UPDATE:{
			TempWindowListEntry* twle = msg->dti.userPointer;

			if(	twle &&
				!twl->flags.ignoreUpdates)
			{
				TempWindowListEntry*	twleThread;
				TempWindowListEntry*	twleTimerID = twle->timerInstance.twleTimerID;
				S32						isCycles = 1;

				assert(twle->twleType == TWLE_TIMER_INSTANCE);
				
				if(twleTimerID){
					isCycles = twleTimerID->timerID.timerType == PERFINFO_TYPE_CPU;
				}

				twleThread = twle->twleThread;

				if(FALSE_THEN_SET(twleThread->thread.flags.didSetNewFrame)){
					twleThread->thread.frameCount++;
				}

				// Add to the cycles history.
				
				timerHistoryFrameAddValues(	&twle->timerInstance.history,
											twleThread->thread.frameCount,
											msg->dtiFrameUpdate.count,
											isCycles ?
												msg->dtiFrameUpdate.cyclesActive :
												msg->dtiFrameUpdate.cyclesActiveChildren,
											msg->dtiFrameUpdate.cyclesBlocking,
											0,
											isCycles ?
												0 :
												msg->dtiFrameUpdate.cyclesActive,
											0,
											0,
											0);
				
				if(!twle->le){
					twleCreateListEntry(twle, twl);
				}
				
				if(	twleTimerID &&
					!twle->timerInstance.count.hits)
				{
					twleTimerID->timerID.timerInstanceCount++;
				}

				twle->timerInstance.count.frames++;
				twle->timerInstance.count.hits += msg->dtiFrameUpdate.count;
				
				if(isCycles){
					twle->timerInstance.cycles.total +=	msg->dtiFrameUpdate.cyclesActive +
														msg->dtiFrameUpdate.cyclesBlocking;
					twle->timerInstance.cycles.active += msg->dtiFrameUpdate.cyclesActive;
					twle->timerInstance.cycles.blocking += msg->dtiFrameUpdate.cyclesBlocking;
				}else{
					twle->timerInstance.cycles.total +=	msg->dtiFrameUpdate.cyclesActiveChildren +
														msg->dtiFrameUpdate.cyclesBlocking;
					twle->timerInstance.cycles.active += msg->dtiFrameUpdate.cyclesActiveChildren;
					twle->timerInstance.cycles.blocking += msg->dtiFrameUpdate.cyclesBlocking;
					twle->timerInstance.cycles.other +=	msg->dtiFrameUpdate.cyclesActive;
				}

				assert(twle->timerInstance.count.hits);

				if(	twleTimerID &&
					twleTimerID == SAFE_MEMBER(twle->timerInstance.twleCopy, parent))
				{
					// Count non-recursed timers towards total.

					twleTimerID->timerID.count.hits += msg->dtiFrameUpdate.count;

					if(isCycles){
						twleTimerID->timerID.cycles.total +=	msg->dtiFrameUpdate.cyclesActive +
																msg->dtiFrameUpdate.cyclesBlocking;
						twleTimerID->timerID.cycles.active += msg->dtiFrameUpdate.cyclesActive;
					}else{
						twleTimerID->timerID.cycles.total +=	msg->dtiFrameUpdate.cyclesActiveChildren +
																msg->dtiFrameUpdate.cyclesBlocking;
						twleTimerID->timerID.cycles.active += msg->dtiFrameUpdate.cyclesActiveChildren;
						twleTimerID->timerID.cycles.other += msg->dtiFrameUpdate.cyclesActive;
					}

					twleTimerID->timerID.cycles.blocking += msg->dtiFrameUpdate.cyclesBlocking;

					if(twleTimerID->timerID.frameWhenUpdated != twleThread->thread.frameCount){
						twleTimerID->timerID.frameWhenUpdated = twleThread->thread.frameCount;
						twleTimerID->timerID.count.frames++;
					}

					timerHistoryFrameAddValues(	&twleTimerID->timerID.history,
												twleThread->thread.frameCount,
												msg->dtiFrameUpdate.count,
												isCycles ?
													msg->dtiFrameUpdate.cyclesActive :
													msg->dtiFrameUpdate.cyclesActiveChildren,
												msg->dtiFrameUpdate.cyclesBlocking,
												0,
												isCycles ?
													0 :
													msg->dtiFrameUpdate.cyclesActive,
												0,
												0,
												0);
				}
			}
		}

		xcase AT_DECODER_MSG_DTI_FLAGS_UPDATE:{
			TempWindowListEntry*	twleThread = msg->dt.userPointer;
			U32						instanceID = msg->dtiFlagsUpdate.instanceID;

			if(twleThread){
				TempWindowListEntry* twle;

				if(twleFindTimerInstanceID(twleThread, instanceID, &twle)){
					twle->timerInstance.flags.isBreakpoint = msg->dtiFlagsUpdate.flags.isBreakpoint;
					twle->timerInstance.flags.forcedOpen = msg->dtiFlagsUpdate.flags.forcedOpen;
					twle->timerInstance.flags.forcedClosed = msg->dtiFlagsUpdate.flags.forcedClosed;

					{
						S32 isClosed =	twle->timerInstance.flags.parentNotOpen
										||
										!twle->timerInstance.flags.forcedOpen &&
										twle->timerInstance.flags.closedByDepth
										||
										twle->timerInstance.flags.forcedClosed;

						EARRAY_CONST_FOREACH_BEGIN(twle->children, i, isize);
							twlePropagateParentNotOpen(	twle->children[i],
														isClosed);
						EARRAY_FOREACH_END;
					}
				}
			}
		}
	}
}

static S32 twGetVisibleList(TempWindowWD* wd,
							TempWindowList** twlOut)
{
	EARRAY_CONST_FOREACH_BEGIN(wd->twls, i, isize);
		TempWindowList* twl = wd->twls[i];

		if(twl->flags.isVisible){
			*twlOut = twl;
			return 1;
		}
	EARRAY_FOREACH_END;

	return 0;
}

static void twReflow(TempWindowWD* wd){
	SUIWindow*		w = wd->w;
	S32 			sx;
	S32 			sy;
	TempWindowList* twlVisible = NULL;
	S32				xButtonsLeft;

	if(	!twGetVisibleList(wd, &twlVisible) &&
		eaSize(&wd->twls))
	{
		twlVisible = wd->twls[0];
		twlVisible->flags.isVisible = 1;
	}

	if(twlVisible){
		suiButtonSetText(	wd->wButtonPauseToggle,
							twlVisible->flags.ignoreUpdates ?
								"Unpause" :
								"Pause");
	}

	suiWindowParentGetSize(w, &sx, &sy);

	suiWindowSetSize(w, sx, sy);

	// Place the buttons.

	{
		S32 xSep = 0;
		
		xButtonsLeft = sx - 10 + xSep;

		#define PLACE_NO_RESIZE(w)	xButtonsLeft -= xSep + suiWindowGetSizeX(w);	\
									suiWindowSetPos(w, xButtonsLeft, 10)
		#define PLACE(w)			suiButtonFitToText(w, 0, 25);		\
									PLACE_NO_RESIZE(w)
		#define PLACE_OR_HIDE(w)	if(twlVisible){PLACE(w);}else{suiWindowSetSize(w, 0, 0);}

		PLACE_OR_HIDE(wd->wButtonYScaleInc);
		PLACE_OR_HIDE(wd->wButtonYScaleDec);

		PLACE_OR_HIDE(wd->wButtonXScaleInc);
		PLACE_OR_HIDE(wd->wButtonXScaleDec);

		//PLACE(wd->wButtonBreakPointToggle);
		//PLACE(wd->wButtonUseFrameToggle);

		PLACE_OR_HIDE(wd->wButtonIndentInc);
		PLACE_OR_HIDE(wd->wButtonIndentDec);

		suiWindowSetSize(	wd->wButtonShownData,
							suiWindowGetSizeY(wd->wButtonIndentDec),
							suiWindowGetSizeY(wd->wButtonIndentDec));
		PLACE_NO_RESIZE(wd->wButtonShownData);

		suiWindowSetSize(	wd->wButtonCyclesOnlyToggle,
							suiWindowGetSizeY(wd->wButtonIndentDec),
							suiWindowGetSizeY(wd->wButtonIndentDec));
		PLACE_NO_RESIZE(wd->wButtonCyclesOnlyToggle);

		PLACE_OR_HIDE(wd->wButtonPauseToggle);
		PLACE_OR_HIDE(wd->wButtonClear);

		//PLACE(wd->wButtonOptions);
		
		xButtonsLeft -= 10;
		
		#undef PLACE
		#undef PLACE_NO_RESIZE
	}

	// Place the left list.

	suiWindowSetPosAndSize(wd->leftList.w, 10, 10, wd->leftList.width, sy - 10 - 10);

	{
		S32 xTabsLeft;
		S32 xCur;
		S32	sxView = sx - 10 - (10 + wd->leftList.width + 10);
		S32	syView = sy - 10 - 40;

		suiButtonSetText(	wd->wButtonLeftListToggle,
							wd->leftList.widthTarget ? "<" : ">");

		suiButtonFitToText(wd->wButtonLeftListToggle, 0, 25);
		suiWindowSetPos(wd->wButtonLeftListToggle,
						10 + wd->leftList.width + 3,
						6);

		xTabsLeft =	suiWindowGetPosX(wd->wButtonLeftListToggle) +
					suiWindowGetSizeX(wd->wButtonLeftListToggle) +
					5;

		xCur = xTabsLeft;

		suiWindowSetPosAndSize(	wd->wViewContainer,
								10 + wd->leftList.width + 10,
								40,
								sxView,
								syView);

		EARRAY_CONST_FOREACH_BEGIN(wd->twls, i, isize);
			TempWindowList* twl = wd->twls[i];

			if(!twl->wFrame){
				suiButtonFitToText(twl->wTabButton, 80, 0);

				if(twl->flags.isVisible){
					suiWindowSetPos(twl->wTabButton, xCur, 10);
				}else{
					suiWindowSetSizeY(twl->wTabButton, suiWindowGetSizeY(twl->wTabButton) + 4);
					suiWindowSetPos(twl->wTabButton, xCur, 6);
				}

				xCur += suiWindowGetSizeX(twl->wTabButton);

				if(twl->flags.isVisible){
					suiWindowSetPosAndSize(	twl->wList,
											0,
											0,
											sxView,
											syView);
				}else{
					suiWindowSetSize(twl->wList, 0, 0);
				}
			}
		EARRAY_FOREACH_END;
		
		if(xCur > xButtonsLeft){
			S32 xSizeOrig = xCur - xTabsLeft;
			S32 xSizeMax = xButtonsLeft - xTabsLeft;
			
			xCur = xTabsLeft;

			EARRAY_CONST_FOREACH_BEGIN(wd->twls, i, isize);
				TempWindowList* twl = wd->twls[i];

				if(!twl->wFrame){
					if(xSizeMax <= 0){
						suiWindowSetSizeX(twl->wTabButton, 0);
					}else{
						S32 xSize = xSizeMax *
									suiWindowGetSizeX(twl->wTabButton) /
									xSizeOrig;

						suiWindowSetPosX(twl->wTabButton, xCur);
						suiWindowSetSizeX(twl->wTabButton, xSize);
						
						xCur += xSize;
					}
				}
			EARRAY_FOREACH_END;
		}
	}

	suiWindowInvalidate(w);
}

static void twSetCurrentList(	TempWindowWD* wd,
								TempWindowList* twl)
{
	S32 doReflow = 0;

	EARRAY_CONST_FOREACH_BEGIN(wd->twls, i, isize);
		TempWindowList* twlCur = wd->twls[i];
		S32				isVisible;

		if(twl->wFrame){
			continue;
		}

		isVisible = twlCur == twl;

		if(twlCur->flags.isVisible != isVisible){
			twlCur->flags.isVisible = isVisible;
			doReflow = 1;
		}
	EARRAY_FOREACH_END;

	if(doReflow){
		twReflow(wd);
	}
}

static S32 detailViewHandleDraw(SUIWindow* w,
								DetailViewWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIDrawContext*	dc = msg->msgData;
	TempWindowList*			twl = wd->twl;
	S32						sx;
	S32						sy;
	TimerHistoryFrame		hfCycles;
	TimerHistoryFrame		hfFrame;
	char					buffer[1000];
	S32						y;
	const S32				x1 = 10;
	const S32				x2 = 10 + 110;
	const S32				x3 = 10 + 110 * 2;
	const S32				x4 = 10 + 110 * 3;
	const U32				argbCycles = 0xffff5555;
	const U32				argbCyclesBlocking = 0xff5555ff;
	const U32				argbCyclesTotal = 0xff55ff55;
	U64						frameCyclesActive;
	U64						frameCyclesBlocking;
	U64						cyclesActive;

	suiWindowGetSize(w, &sx, &sy);

	suiDrawRect(dc, 0, 0, sx, sy, 2, 0xffffffff);
	suiDrawRect(dc, 2, 2, sx - 4, sy - 4, 2, 0xff000000);
	suiDrawFilledRect(dc, 4, 4, sx - 8, sy - 8, 0xff111111);

	if(!wd->clCycles){
		return 1;
	}

	timerHistoryGetExistingFrame(	wd->clCycles,
									wd->selectedFrameIndex,
									&hfCycles);

	timerHistoryGetExistingFrame(	wd->clFrames,
									wd->selectedFrameIndex,
									&hfFrame);

	drawHistoryGraph(	dc,
						wd->shownData,
						x1,
						10,
						sx - 20,
						sy - 140,
						wd->clCycles,
						wd->clFrames,
						twl->cyclesMax - twl->cyclesViewedDelta,
						twl->cyclesMax,
						wd->shownData == SHOWN_DATA_HIT_COUNT ?
							twl->scaleCount :
							twl->scaleCycles * SQR((U64)1000),
						twl->selectedCyclesBegin,
						twl->selectedCyclesDelta,
						0,
						wd->showOtherValue,
						wd->shownData == SHOWN_DATA_HIT_COUNT ?
							ARGB_HIT_COUNT :
							wd->showOtherValue ?
								ARGB_NON_CYCLES :
								ARGB_CYCLES_ACTIVE,
						0);

	suiDrawRect(dc,
				x1 - 1,
				10 - 1,
				sx - 20 + 2,
				sy - 140 + 2,
				1,
				0xff666666);

	y = sy - 140;

	suiPrintText(dc, x1, y += 15, "Name: ", -1, 15, 0xffffffff);
	suiPrintText(dc, x2, y, wd->name, -1, 15, 0xffffffff);

	sprintf(buffer, "%d", wd->selectedFrameIndex);
	suiPrintText(dc, x1, y += 15, "Frame: ", -1, 15, 0xffffffff);
	suiPrintText(dc, x2, y, buffer, -1, 15, 0xffffffff);

	switch(wd->showOtherValue){
		xcase 1:
			frameCyclesActive = hfFrame.cycles.active;
			frameCyclesBlocking = hfFrame.cycles.blocking;
			cyclesActive = hfCycles.cycles.other;
		xcase 2:
			frameCyclesActive = hfFrame.cycles.os;
			frameCyclesBlocking = 0;
			cyclesActive = hfCycles.cycles.os;
		xcase 3:
			frameCyclesActive = hfFrame.osTicks.user;
			frameCyclesBlocking = 0;
			cyclesActive = hfCycles.osTicks.user;
		xcase 4:
			frameCyclesActive = hfFrame.osTicks.kernel;
			frameCyclesBlocking = 0;
			cyclesActive = hfCycles.osTicks.kernel;
		xdefault:
			frameCyclesActive = hfFrame.cycles.active;
			frameCyclesBlocking = hfFrame.cycles.blocking;
			cyclesActive = hfCycles.cycles.active;
	}

	if(hfFrame.frameIndex){
		const U64 cyclesTotal =	hfFrame.cycles.active +
								hfFrame.cycles.blocking;
		
		suiPrintText(dc, x1, y += 15, "Frame Cycles: ", -1, 15, 0xffffffff);

		if(cyclesTotal){
			suiPrintText(dc, x2, y, getCommaSeparatedU64(cyclesTotal), -1, 15, argbCyclesTotal);
			
			if(frameCyclesActive){
				suiPrintText(dc, x3, y, getCommaSeparatedU64(frameCyclesActive), -1, 15, argbCycles);
			}

			if(frameCyclesBlocking){
				suiPrintText(dc, x4, y, getCommaSeparatedU64(frameCyclesBlocking), -1, 15, argbCyclesBlocking);
			}
		}
	}

	if(hfCycles.frameIndex){
		if(hfCycles.count){
			{
				const U64 cyclesTotal =	cyclesActive +
										hfCycles.cycles.blocking;

				suiPrintText(dc, x1, y += 15, "Cycles: ", -1, 15, 0xffffffff);
				if(cyclesTotal){
					suiPrintText(dc, x2, y, getCommaSeparatedU64(cyclesTotal), -1, 15, argbCyclesTotal);
					if(cyclesActive){
						suiPrintText(dc, x3, y, getCommaSeparatedU64(cyclesActive), -1, 15, argbCycles);
					}
					if(hfCycles.cycles.blocking){
						suiPrintText(dc, x4, y, getCommaSeparatedU64(hfCycles.cycles.blocking), -1, 15, argbCyclesBlocking);
					}
				}

				suiPrintText(dc, x1, y += 15, "Count: ", -1, 15, 0xffffffff);
				suiPrintText(dc, x2, y, getCommaSeparatedU64(hfCycles.count), -1, 15, 0xffffffff);

				suiPrintText(dc, x1, y += 15, "Average: ", -1, 15, 0xffffffff);
				if(cyclesTotal){
					suiPrintText(dc, x2, y, getCommaSeparatedU64((cyclesTotal) / hfCycles.count), -1, 15, argbCyclesTotal);
					if(cyclesActive){
						suiPrintText(dc, x3, y, getCommaSeparatedU64(cyclesActive / hfCycles.count), -1, 15, argbCycles);
					}
					if(hfCycles.cycles.blocking){
						suiPrintText(dc, x4, y, getCommaSeparatedU64(hfCycles.cycles.blocking / hfCycles.count), -1, 15, argbCyclesBlocking);
					}
				}
			}

			if(hfFrame.frameIndex){
				suiPrintText(dc, x1, y += 15, "Percent: ", -1, 15, 0xffffffff);

				FOR_BEGIN(i, 3);
					U64 frameCyclesTotal;
					U64 cyclesTotal;
					S32 x;
					U32 argb;

					switch(i){
						xcase 0:{
							x = x2;
							argb = argbCyclesTotal;
							frameCyclesTotal =	hfFrame.cycles.active +
												hfFrame.cycles.blocking;
							cyclesTotal =	cyclesActive +
											hfCycles.cycles.blocking;
						}
						xcase 1:{
							x = x3;
							argb = argbCycles;
							frameCyclesTotal = hfFrame.cycles.active;
							cyclesTotal = cyclesActive;
						}
						xcase 2:{
							x = x4;
							argb = argbCyclesBlocking;
							frameCyclesTotal = hfFrame.cycles.blocking;
							cyclesTotal = hfCycles.cycles.blocking;
						}
					}

					if(	cyclesTotal &&
						frameCyclesTotal)
					{
						F32 percent = 100.f * (F32)cyclesTotal / frameCyclesTotal;

						sprintf(buffer,
								FORMAT_OK(	percent >= 99.995f ?
												"%.2f" :
												percent >= 9.9995f ?
													"%.3f" :
													"%.4f"),
								percent);

						suiPrintText(dc, x, y, buffer, -1, 15, argb);
					}
				FOR_END;

				suiPrintText(dc, x1, y += 15, "Percent of Total: ", -1, 15, 0xffffffff);

				FOR_BEGIN(i, 3);
					U64 frameCyclesTotal =	hfFrame.cycles.active +
											hfFrame.cycles.blocking;
					U64 cyclesTotal;
					S32 x;
					U32 argb;

					switch(i){
						xcase 0:{
							continue;
						}
						xcase 1:{
							x = x3;
							argb = argbCycles;
							cyclesTotal = cyclesActive;
						}
						xcase 2:{
							x = x4;
							argb = argbCyclesBlocking;
							cyclesTotal = hfCycles.cycles.blocking;
						}
					}

					if(	cyclesTotal &&
						frameCyclesTotal)
					{
						F32 percent = 100.f * (F32)cyclesTotal / frameCyclesTotal;

						sprintf(buffer,
								FORMAT_OK(	percent >= 99.995f ?
												"%.2f" :
												percent >= 9.9995f ?
													"%.3f" :
													"%.4f"),
								percent);

						suiPrintText(dc, x, y, buffer, -1, 15, argb);
					}
				FOR_END;
			}
		}
	}

	return 1;
}

static S32 detailViewHandleProcess(	SUIWindow* w,
									DetailViewWD* wd,
									const SUIWindowMsg* msg)
{
	S32 y;
	S32 diff;
	U32 msCur;
	F32 scale;
	
	PERFINFO_AUTO_START_FUNC();

	y = suiWindowGetPosY(w);
	msCur = timeGetTime();

	scale = (F32)(msCur - wd->msStart) / 200.f;
	MINMAX1(scale, 0.f, 1.f);
	scale = SQR(1.f - scale);

	diff = wd->yStart - wd->yTarget;

	y = wd->yTarget + diff * scale;

	suiWindowSetPosY(w, y);

	if(y == wd->yTarget){
		suiWindowProcessingHandleDestroy(w, &wd->ph);
		wd->msStart = 0;
	}
	
	PERFINFO_AUTO_STOP();

	return 1;
}

static S32 detailViewMsgHandler(SUIWindow* w,
								DetailViewWD* wd,
								const SUIWindowMsg* msg)
{
	SUI_WM_DEFAULT_HANDLERS_BEGIN(w, msg, DetailViewWD, wd);
		SUI_WM_CASE(SUI_WM_CREATE){
			const DetailViewCreateParams* cp = msg->msgData;

			assert(!wd);

			wd = callocStruct(DetailViewWD);

			wd->twl = cp->twl;

			suiWindowSetUserPointer(w, detailViewMsgHandler, wd);
		}

		SUI_WM_CASE(SUI_WM_DESTROY){
			estrDestroy(&wd->name);
			SAFE_FREE(wd);
		}

		SUI_WM_HANDLER(SUI_WM_DRAW,		detailViewHandleDraw);
		SUI_WM_HANDLER(SUI_WM_PROCESS,	detailViewHandleProcess);
	SUI_WM_HANDLERS_END;

	return 0;
}

static void detailViewSetTarget(SUIWindow* w,
								S32 y,
								U32 selectedFrameIndex,
								const char* name,
								const TimerHistoryChunkList* clCycles,
								const TimerHistoryChunkList* clFrames,
								ShownDataType shownData,
								S32 showOtherValue)
{
	DetailViewWD* wd;

	if(suiWindowGetUserPointer(w, detailViewMsgHandler, &wd)){
		estrCopy2(&wd->name, name);
		wd->clCycles = clCycles;
		wd->clFrames = clFrames;
		wd->selectedFrameIndex = selectedFrameIndex;
		wd->shownData = shownData;
		wd->showOtherValue = showOtherValue;

		if(clCycles){
			assert(clFrames);
		}

		if(wd->yTarget != y){
			wd->yTarget = y;

			wd->msStart = timeGetTime();
			wd->yStart = suiWindowGetPosY(w);

			if(!wd->ph){
				suiWindowProcessingHandleCreate(w, &wd->ph);
			}
		}
	}
}

typedef struct TextEntryPopupWD {
	SUIWindow*								w;
	//TempWindowWD*							wdOwner;

	SUIWindowPipe*							wp;

	SUIWindowProcessingHandle*				ph;

	S32										ySource;
	S32										yTarget;
	U32										msStartMove;

	char*									promptText;
	char*									inputText;
} TextEntryPopupWD;

typedef struct TextEntryPopupCreateParams {
	//TempWindowWD*							wdOwner;
	SUIWindow*								wReader;
} TextEntryPopupCreateParams;

typedef enum TextEntryPopupMsgType {
	TEXT_ENTRY_POPUP_MSG_OK,
	TEXT_ENTRY_POPUP_MSG_CANCEL,
} TextEntryPopupMsgType;

typedef struct TextEntryPopupMsg {
	const char*								inputText;
} TextEntryPopupMsg;

SUI_MSG_GROUP_FUNCTION_DEFINE(textEntryPopup, "TextEntryPopup");

static S32 textEntryPopupMsgHandler(SUIWindow* w,
									TextEntryPopupWD* wd,
									const SUIWindowMsg* msg);

static void textEntryPopupSetPromptText(SUIWindow* w,
										const char* text)
{
	TextEntryPopupWD* wd;

	if(suiWindowGetUserPointer(w, textEntryPopupMsgHandler, &wd)){
		estrCopy2(&wd->promptText, text);
		suiWindowInvalidate(w);
	}
}

static void textEntryPopupSetTarget(SUIWindow* w,
									S32 y,
									S32 enableInput,
									const char* text)
{
	TextEntryPopupWD* wd;

	if(suiWindowGetUserPointer(w, textEntryPopupMsgHandler, &wd)){
		wd->yTarget = y;
		wd->msStartMove = timeGetTime();
		wd->ySource = suiWindowGetPosY(w);

		if(text){
			estrCopy2(&wd->inputText, text);
		}

		if(!wd->ph){
			suiWindowProcessingHandleCreate(w, &wd->ph);
		}

		suiWindowSetKeyboardExclusive(w, !!enableInput);
	}
}

static S32 textEntryPopupMsgHandler(SUIWindow* w,
									TextEntryPopupWD* wd,
									const SUIWindowMsg* msg)
{
	SUI_WM_DEFAULT_HANDLERS_BEGIN(w, msg, TextEntryPopupWD, wd);
		SUI_WM_CASE(SUI_WM_CREATE){
			const TextEntryPopupCreateParams* cp = msg->msgData;

			assert(!wd);

			wd = callocStruct(TextEntryPopupWD);

			//wd->wdOwner = cp->wdOwner;

			suiWindowPipeCreate(&wd->wp,
								cp->wReader,
								w);

			suiWindowSetUserPointer(w, textEntryPopupMsgHandler, wd);
		}

		SUI_WM_CASE(SUI_WM_DESTROY){
			suiWindowProcessingHandleDestroy(w, &wd->ph);
			estrDestroy(&wd->promptText);
			estrDestroy(&wd->inputText);
			SAFE_FREE(wd);
		}

		SUI_WM_CASE(SUI_WM_DRAW){
			const SUIDrawContext*	dc = msg->msgData;
			S32						sx;
			S32						sy;

			suiWindowGetSize(w, &sx, &sy);

			suiDrawRect(dc, 0, 0, sx, sy, 2, 0xff4444ff);
			suiDrawRect(dc, 2, 2, sx - 4, sy - 4, 2, 0xff000000);
			suiDrawFilledRect(dc, 4, 4, sx - 8, sy - 8, 0xff552222);

			suiPrintText(dc, 11, 11, wd->promptText, -1, 30, 0xff000000);
			suiPrintText(dc, 10, 10, wd->promptText, -1, 30, 0xffff8000);

			suiDrawFilledRect(dc, 10, 45, sx - 20, sy - 10 - 45, 0xff111111);
			suiPrintText(dc, 16, 51, wd->inputText, -1, sy - 10 - 45 - 5, 0xff000000);
			suiPrintText(dc, 15, 50, wd->inputText, -1, sy - 10 - 45 - 5, 0xff0080ff);
		}

		SUI_WM_CASE(SUI_WM_PROCESS){
			S32 y = suiWindowGetPosY(w);
			F32 scale = (timeGetTime() - wd->msStartMove) / 500.f;

			MINMAX1(scale, 0.f, 1.f);

			scale = 1.f - SQR(1.f - scale);

			y = wd->ySource + (wd->yTarget - wd->ySource) * scale;

			suiWindowSetPosY(w, y);

			if(scale >= 1.f){
				suiWindowProcessingHandleDestroy(w, &wd->ph);
			}
		}

		SUI_WM_CASE(SUI_WM_KEY_DOWN){
			const SUIWindowMsgKey* md = msg->msgData;

			if(!md->flags.isOwned){
				break;
			}

			switch(md->key){
				xcase SUI_KEY_ENTER:{
					TextEntryPopupMsg mdNew;

					mdNew.inputText = wd->inputText;

					suiWindowPipeMsgSend(	wd->wp,
											w,
											NULL,
											SUI_MSG_GROUP(textEntryPopup),
											TEXT_ENTRY_POPUP_MSG_OK,
											&mdNew);

					estrDestroy(&wd->promptText);
					estrDestroy(&wd->inputText);
					textEntryPopupSetTarget(w, -suiWindowGetSizeY(w), 0, NULL);
				}

				xcase SUI_KEY_ESCAPE:{
					estrDestroy(&wd->promptText);
					estrDestroy(&wd->inputText);
					textEntryPopupSetTarget(w, -suiWindowGetSizeY(w), 0, NULL);

					suiWindowPipeMsgSend(	wd->wp,
											w,
											NULL,
											SUI_MSG_GROUP(textEntryPopup),
											TEXT_ENTRY_POPUP_MSG_CANCEL,
											NULL);
				}

				xcase SUI_KEY_BACKSPACE:{
					if(estrLength(&wd->inputText)){
						estrSetSize(&wd->inputText, estrLength(&wd->inputText) - 1);
						suiWindowInvalidate(w);
					}
				}

				xdefault:{
					if(	md->modBits & SUI_KEY_MOD_CONTROL &&
						md->key == SUI_KEY_V)
					{
						estrConcatf(&wd->inputText, "%s", winCopyFromClipboard());
						suiWindowInvalidate(w);
					}
					else if(md->character){
						estrConcatf(&wd->inputText, "%c", md->character);
						suiWindowInvalidate(w);
					}
				}
			}

			return 1;
		}
	SUI_WM_HANDLERS_END;

	return 0;
}

static S32 containerMsgHandler(	SUIWindow* w,
								void* wd,
								const SUIWindowMsg* msg)
{
	return 0;
}

static S32 lleNetClientProfilerCreatePacket(LeftListEntry* lle,
											Packet** pakOut,
											const char* cmdName)
{
	if(	lle->lleType != LLE_NET_CLIENT_PROFILER ||
		!lle->netClient.link)
	{
		return 0;
	}

	*pakOut = pktCreate(lle->netClient.link, 0);
	
	pktSendString(*pakOut, cmdName);

	return 1;
}

static void lleSendToClientMyInfo(LeftListEntry* lle){
	Packet* pak;
	
	if(lleNetClientProfilerCreatePacket(lle, &pak, "MyInfo")){
		pktSendString(pak, getComputerName());
		pktSendString(pak, getUserName());

		pktSend(&pak);
	}
}

static void lleSendToClientProcessAdd(	LeftListEntry* lle,
										const LeftListEntry* lleProcess)
{
	Packet* pak;
	
	assert(lleProcess->lleType == LLE_PROCESS);
	assert(lleProcess->process.flags.isLocal);

	if(lleNetClientProfilerCreatePacket(lle, &pak, "ProcessAdd")){
		pktSendString(pak, lleProcess->process.exeName);
		pktSendBitsAuto(pak, lleProcess->process.pid);
		pktSendBitsAuto(pak, lleProcess->process.pidInstance);
		pktSend(&pak);
	}
}

static void lleSendProcessID(	Packet* pak,
								U32 pid,
								U32 pidInstance)
{
	pktSendBitsAuto(pak, pid);
	pktSendBitsAuto(pak, pidInstance);
}

static void lleSendToClientProcessRemove(	LeftListEntry* lle,
											U32 pid,
											U32 pidInstance)
{
	Packet* pak;
	
	if(lleNetClientProfilerCreatePacket(lle, &pak, "ProcessRemove")){
		lleSendProcessID(pak, pid, pidInstance);
		pktSend(&pak);
	}
}

static void lleSendToClientReceivedAnything(LeftListEntry* lle,
											U32 pid,
											U32 pidInstance)
{
	Packet* pak;
	
	if(lleNetClientProfilerCreatePacket(lle, &pak, "ReceivedAnything")){
		lleSendProcessID(pak, pid, pidInstance);
		pktSend(&pak);
	}
}

static void lleSendToClientTimerStopped(LeftListEntry* lle,
										U32 pid,
										U32 pidInstance,
										U32 startID)
{
	Packet* pak;

	if(lleNetClientProfilerCreatePacket(lle, &pak, "TimerStopped")){
		lleSendProcessID(pak, pid, pidInstance);
		pktSendBitsAuto(pak, startID);
		pktSend(&pak);
	}
}

static void lleSendToClientThreadsAreActive(LeftListEntry* lle,
											U32 pid,
											U32 pidInstance,
											S32 isActive)
{
	Packet* pak;

	if(lleNetClientProfilerCreatePacket(lle, &pak, "ThreadsAreActive")){
		lleSendProcessID(pak, pid, pidInstance);
		pktSendBits(pak, 1, !!isActive);
		pktSend(&pak);
	}
}

static void lleSendToClientProcessTimes(LeftListEntry* lle,
										const LeftListEntry* lleProcess)
{
	Packet* pak;

	assert(lleProcess->lleType == LLE_PROCESS);

	if(lleNetClientProfilerCreatePacket(lle, &pak, "ProcessTimes")){
		lleSendProcessID(pak, lleProcess->process.pid, lleProcess->process.pidInstance);

		if(!lleProcess->process.osTimes.cycles){
			pktSendBits(pak, 1, 0);
		}else{
			pktSendBits(pak, 1, 1);
			pktSendBits64(pak, 64, lleProcess->process.osTimes.cycles);
		}
		
		if(!lleProcess->process.osTimes.ticksUser){
			pktSendBits(pak, 1, 0);
		}else{
			pktSendBits(pak, 1, 1);
			pktSendBits64(pak, 64, lleProcess->process.osTimes.ticksUser);
		}

		if(!lleProcess->process.osTimes.ticksKernel){
			pktSendBits(pak, 1, 0);
		}else{
			pktSendBits(pak, 1, 1);
			pktSendBits64(pak, 64, lleProcess->process.osTimes.ticksKernel);
		}

		pktSend(&pak);
	}
}

static void lleSendToClientYourClientType(	LeftListEntry* lle,
											const char* clientTypeName){
	Packet* pak;
	
	if(lleNetClientProfilerCreatePacket(lle, &pak, "YourClientType")){
		pktSendString(pak, clientTypeName);
		pktSend(&pak);
	}
}


static void twListCreate(	TempWindowList** twlOut,
							TempWindowWD* wd,
							LeftListEntry* lle,
							S32 createReader,
							const char* name,
							const char* fileName);

static void lleCreateTempWindowList(LeftListEntry* lle){
	LeftListEntryProcess*	p = &lle->process;
	char					name[200] = "";
	LeftListEntry*			lleHost;

	assert(lle->lleType == LLE_PROCESS);

	lleHost = lle->parent;

	assert(lleHost->lleType == LLE_HOST);

	sprintf(name,
			"%d:%s",
			p->pid,
			getFileNameConst(p->exeName));

	if(strEndsWith(name, ".exe")){
		name[strlen(name) - 4] = 0;
	}

	if(!p->flags.isLocal){
		strcatf(name,
				" - %s",
				p->remote.lleNetClientProcess ?
					p->remote.lleNetClientProcess->netClient.computerName :
					FIRST_IF_SET(	lleHost->host.computerName,
									lleHost->host.hostName));
	}

	twListCreate(	&p->twl,
					lle->wdOwner,
					lle,
					0,
					name,
					NULL);
}

static void lleLocalProcessSendString(	LeftListEntry* lle,
										const char* text)
{
	LeftListEntryProcess*	p = &lle->process;
	U32						count = 0;
	
	assert(lle->lleType == LLE_PROCESS);
	assert(p->flags.isLocal);
	
	if(	p->local.pc &&
		!pcWriteString(	p->local.pc,
						text,
						NULL))
	{
		eaPush(&p->local.stringsToSend, strdup(text));
	}
}

static void lleSendToHostStartTimer(LeftListEntry* lle){
	LeftListEntryProcess* p = &lle->process;

	assert(lle->lleType == LLE_PROCESS);

	if(!FALSE_THEN_SET(p->flags.timerStarted)){
		return;
	}
	
	if(p->flags.isLocal){
		char cmdText[100];

		p->local.startID = ++p->startID;

		sprintf(cmdText,
				"StartTimerWithID %u",
				p->local.startID);

		lleLocalProcessSendString(lle, cmdText);
	}
	else if(p->remote.lleNetClientProcess){
		LeftListEntry*	lleClient = p->remote.lleNetClientProcess;
		Packet*			pak;
		
		assert(lleClient->lleType == LLE_NET_CLIENT_PROCESS);
		assert(lleClient->netClient.link);
		
		pak = pktCreate(lleClient->netClient.link, 0);
		
		p->startID++;
		p->flags.gotStartID = 0;

		pktSendString(pak, "StartTimer");
		pktSendBitsAuto(pak, p->startID);
		pktSend(&pak);
	}else{
		LeftListEntry*	lleHost = lle->parent;
		Packet*			pak;

		assert(lleHost->lleType == LLE_HOST);
		assert(!lleHost->host.flags.isLocal);
		assert(lleHost->host.link);

		pak = pktCreate(lleHost->host.link, 0);

		p->startID++;

		pktSendString(pak, "StartTimer");
		pktSendBitsAuto(pak, p->pid);
		pktSendBitsAuto(pak, p->pidInstance);
		pktSendBitsAuto(pak, p->startID);

		pktSend(&pak);
	}

	lleCreateTempWindowList(lle);
}

static void lleSendToHostSetTimerFlag(	LeftListEntry* lle,
										const char* cmdName,
										U32 threadID,
										U32 instanceID,
										S32 doEnableFlag)
{
	LeftListEntryProcess*	p = &lle->process;

	assert(lle->lleType == LLE_PROCESS);

	if(p->flags.isLocal){
		char buffer[100];

		sprintf(buffer,
				"%s %d %d %d",
				cmdName,
				threadID,
				instanceID,
				doEnableFlag);

		lleLocalProcessSendString(lle, buffer);
	}
	else if(p->remote.lleNetClientProcess){
		LeftListEntry*	lleClient = p->remote.lleNetClientProcess;
		Packet*			pak;
		char			buffer[100];
		
		sprintf(buffer,
				"%s %d %d %d",
				cmdName,
				threadID,
				instanceID,
				doEnableFlag);
		
		pak = pktCreate(lleClient->netClient.link, 0);

		pktSendString(pak, "SetTimerFlag");
		pktSendString(pak, buffer);
		pktSend(&pak);
	}else{
		LeftListEntry*	lleHost = lle->parent;
		Packet*			pak;

		assert(lleHost->lleType == LLE_HOST);
		assert(!lleHost->host.flags.isLocal);
		assert(lleHost->host.link);

		pak = pktCreate(lleHost->host.link, 0);

		pktSendString(pak, cmdName);
		pktSendBitsAuto(pak, p->pid);
		pktSendBitsAuto(pak, p->pidInstance);
		pktSendBitsAuto(pak, threadID);
		pktSendBitsAuto(pak, instanceID);
		pktSendBits(pak, 1, !!doEnableFlag);

		pktSend(&pak);
	}
}

static void lleSendToClientProcessList(LeftListEntry* lle){
	const LeftListEntry*const* lleProcesses = lle->wdOwner->leftList.lleHostLocal->children;

	EARRAY_CONST_FOREACH_BEGIN(lleProcesses, i, isize);
		const LeftListEntry* lleProcess = lleProcesses[i];

		lleSendToClientProcessAdd(lle, lleProcess);
		lleSendToClientThreadsAreActive(lle,
										lleProcess->process.pid,
										lleProcess->process.pidInstance,
										lleProcess->process.flags.threadsAreActive);

		lleSendToClientProcessTimes(lle,
									lleProcess);

		if(lleProcess->process.flags.receivedAnything){
			lleSendToClientReceivedAnything(lle,
											lleProcess->process.pid,
											lleProcess->process.pidInstance);
		}
	EARRAY_FOREACH_END;
}

static void lleSendToClientFragmentedBuffer(LeftListEntry* lleClientToProcess,
											U32 pid,
											U32 pidInstance,
											FragmentedBuffer* fb)
{
	LeftListEntry*	lleClient = lleClientToProcess->netClientToProcess.lleClient;
	Packet*			pak = pktCreate(lleClient->netClient.link, 0);
	U32				bytesRemaining;
	TempWindowWD*	wd = lleClientToProcess->wdOwner;
	
	if(!wd->fbr){
		fbReaderCreate(&wd->fbr);
	}
	
	fbReaderAttach(wd->fbr, fb, 1);

	fbGetSizeAsBytes(fb, &bytesRemaining);

	pktSendString(pak, "Buffer");
	pktSendBitsAuto(pak, pid);
	pktSendBitsAuto(pak, pidInstance);
	pktSendBitsAuto(pak, lleClientToProcess->netClientToProcess.clientStartID);

	while(bytesRemaining){
		U8	buffer[1000];
		U32 bytesCur = MIN(sizeof(buffer), bytesRemaining);

		pktSendBits(pak, 1, 1);
		pktSendBitsAuto(pak, bytesCur);

		fbReadBuffer(wd->fbr, buffer, bytesCur);

		pktSendBytes(pak, bytesCur, buffer);

		bytesRemaining -= bytesCur;
	}
	pktSendBits(pak, 1, 0);

	fbReaderDetach(wd->fbr);

	pktSend(&pak);
}

static S32 twListAutoTimerReaderMsgHandler(const AutoTimerReaderMsg* msg){
	TempWindowList* twl = msg->userPointer;
	
	switch(msg->msgType){
		xcase ATR_MSG_ANY_THREAD_NEW_BUFFER_AVAILABLE:{
		}

		xcase ATR_MSG_NEW_BUFFER:{
			autoTimerDecoderDecode(twl->atDecoder, msg->newBuffer.fb);
			
			if(timeGetTime() - twl->msStartReading >= 100){
				msg->out->newBuffer.flags.stopSendingBuffers = 1;
			}
		}
	}

	return 1;
}

static void twListCreate(	TempWindowList** twlOut,
							TempWindowWD* wd,
							LeftListEntry* lleProcess,
							S32 createReader,
							const char* name,
							const char* fileName)
{
	SUIWindow*		w = wd->w;
	TempWindowList* twl;

	if(lleProcess){
		assert(lleProcess->lleType == LLE_PROCESS);
	}

	twl = callocStruct(TempWindowList);

	eaPush(&wd->twls, twl);

	twl->wdOwner = wd;
	twl->scaleCycles = 1.f;
	twl->scaleCount = 1.f;
	twl->cyclesViewedDelta = 5 * 2 * CUBE((U64)1000);

	twl->lleProcess = lleProcess;

	twl->name = strdup(name);

	if(wd->flags.useFrame){
		SUIFrameCreateParams cp = {0};

		cp.name = name;
		cp.userPointer = twl;
		cp.wReader = w;

		if(suiFrameWindowCreate(&twl->wFrame, wd->wViewContainer, &cp)){
			suiWindowSetPos(twl->wFrame, 10 + rand() % 50, 44 + rand() % 50);
		}
	}else{
		suiButtonCreate(&twl->wTabButton,
						w,
						name,
						w);
	}

	if(suiListCreateBasic(&twl->wList, NULL, twl, wd->w)){
		const char * pTitleString =  "System Info                                                                                                       Hits" \
										"                    Cycles                             CyclesPerCount             CountPerFrame    CyclesPerFrame      Depth/SubTimer";
		suiListSetXIndentPerDepth(twl->wList, 10);

		if(twl->wFrame){
			suiFrameWindowSetClientWindow(twl->wFrame, twl->wList);
		}else{
			suiWindowAddChild(wd->wViewContainer, twl->wList);
			twSetCurrentList(wd, twl);
		}

		suiListEntryClassCreate(&twl->lec, twl->wList, w);
		
		twleCreate(&twl->twleInfo, TWLE_GROUP, pTitleString, NULL, twl, 1, 1);
	}

	{
		DetailViewCreateParams cp = {0};

		cp.twl = twl;

		if(suiWindowCreate(&twl->wDetailView, twl->wList, detailViewMsgHandler, &cp)){
			suiWindowSetPosAndSize(twl->wDetailView, 50, -300, 800, 300);
		}
	}

	autoTimerDecoderCreate(	&twl->atDecoder,
							twAutoTimerDecoderMsgHandler,
							twl);

	if(createReader){
		if(!wd->localReaderCount++){
			suiWindowProcessingHandleCreate(w, &wd->phLocalReader);
		}
		autoTimerReaderCreate(&twl->atReader, twListAutoTimerReaderMsgHandler, twl);
		autoTimerReaderStreamCreate(twl->atReader, &twl->atReaderStream, NULL);
	}
	else if(SAFE_DEREF(fileName)){
		if(!wd->fileReaderCount++){
			suiWindowProcessingHandleCreate(w, &wd->phFileReader);
		}
		pfrCreate(&twl->pfr);
		pfrStart(twl->pfr, fileName, twl->atDecoder);
	}

	twReflow(wd);

	suiWindowInvalidate(w);

	if(twlOut){
		*twlOut = twl;
	}
}

static void lleSendToHostStopTimer(LeftListEntry* lle);

static void twListDestroy(	TempWindowWD* wd,
							TempWindowList** twlInOut)
{
	TempWindowList* twl = SAFE_DEREF(twlInOut);

	if(!twl){
		return;
	}

	*twlInOut = NULL;

	if(!FALSE_THEN_SET(twl->flags.destroyed)){
		return;
	}

	SAFE_FREE(twl->name);

	suiWindowDestroy(&twl->wTabButton);

	if(twl->lleProcess){
		LeftListEntry* lle = twl->lleProcess;

		assert(lle->lleType == LLE_PROCESS);
		assert(	!lle->process.twl ||
				lle->process.twl == twl);

		lleSendToHostStopTimer(lle);

		suiWindowInvalidate(wd->leftList.w);

		twl->lleProcess = NULL;
		lle->process.twl = NULL;
	}

	suiWindowDestroy(&twl->wList);
	suiWindowDestroy(&twl->wFrame);

	while(twl->twles){
		twleDestroy(twl->twles[0], twl);
	}

	if(twl->atReader){
		autoTimerReaderStreamDestroy(&twl->atReaderStream);
		autoTimerReaderDestroy(&twl->atReader);
		
		assert(wd->localReaderCount);
		if(!--wd->localReaderCount){
			suiWindowProcessingHandleDestroy(wd->w, &wd->phLocalReader);
		}
	}
	
	autoTimerDecoderDestroy(&twl->atDecoder);
	
	if(twl->pfr){
		pfrDestroy(&twl->pfr);
		
		assert(wd->fileReaderCount);
		if(!--wd->fileReaderCount){
			suiWindowProcessingHandleDestroy(wd->w, &wd->phFileReader);
		}
	}

	if(eaFindAndRemove(&wd->twls, twl) < 0){
		assert(0);
	}

	if(!eaSize(&wd->twls)){
		eaDestroy(&wd->twls);

		if(	wd->leftList.width < 30 &&
			!wd->leftList.ph &&
			!wd->flags.dragging)
		{
			wd->leftList.widthTarget = LEFT_LIST_DEFAULT_WIDTH;
			wd->leftList.timeStart = 0;
			suiWindowProcessingHandleCreate(wd->w, &wd->leftList.ph);
		}
	}

	suiWindowInvalidate(wd->w);

	twReflow(wd);

	SAFE_FREE(twl);
}

static void twleClearData(TempWindowListEntry* twle){
	switch(twle->twleType){
		xcase TWLE_THREAD:{
			twle->thread.flags.hasVisibleTimerIDs = 0;

			twle->thread.byteCountReceived = 0;
			twle->thread.frameCount = 0;
			twle->thread.scanFrameCount = 0;
			twle->thread.scanCycles = 0;

			ZeroStruct(&twle->thread.cycles);
			ZeroStruct(&twle->thread.os);
			ZeroStruct(&twle->thread.locked);

			twle->thread.byteCountChunks = 0;
			timerHistoryChunkListClear(&twle->thread.history);
			timerHistoryChunkListClear(&twle->thread.scanHistory);
			
			twleSetHiddenState(twle->thread.twle.scanFrames, 1);
		}

		xcase TWLE_TIMER_INSTANCE:{
			twle->timerInstance.count.hits = 0;
			twle->timerInstance.count.frames = 0;

			ZeroStruct(&twle->timerInstance.cycles);

			timerHistoryChunkListClear(&twle->timerInstance.history);

			suiListEntryDestroy(&twle->le);
		}

		xcase TWLE_TIMER_ID:{
			twle->timerID.count.hits = 0;
			twle->timerID.count.frames = 0;
			twle->timerID.timerInstanceCount = 0;

			ZeroStruct(&twle->timerID.cycles);

			timerHistoryChunkListClear(&twle->timerID.history);

			suiListEntryDestroy(&twle->le);
		}
		
		xcase TWLE_COPY:{
			suiListEntryDestroy(&twle->le);
		}
	}
	
	EARRAY_CONST_FOREACH_BEGIN(twle->children, i, isize);
		twleClearData(twle->children[i]);
	EARRAY_FOREACH_END;
}

static void twListClearData(TempWindowList* twl){
	PERFINFO_AUTO_START_FUNC();
	
	twl->cyclesMax = 0;
	twl->cyclesMaxLocked = 0;
	twl->cyclesMaxReceived = 0;
	twl->cyclesMinReceived = 0;

	EARRAY_CONST_FOREACH_BEGIN(twl->twles, i, isize);
		twleClearData(twl->twles[i]);
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

static void lleHostConnect(LeftListEntry* lle);
static void lleHostDisconnect(LeftListEntry* lle);

static void lleSetText(	LeftListEntry* lle,
						const char* text)
{
	SAFE_FREE(lle->text);
	
	if(text){
		lle->text = strdup(text);
	}
}

static void lleCreate(	LeftListEntry** lleOut,
						TempWindowWD* wd,
						LeftListEntry* lleParent,
						LeftListEntryType lleType,
						const char* text,
						U32 height)
{
	LeftListEntry* lle;

	lle = callocStruct(LeftListEntry);

	if(lleOut){
		*lleOut = lle;
	}

	lle->wdOwner = wd;

	lle->lleType = lleType;

	lleSetText(lle, text);

	if(lleParent){
		lle->parent = lleParent;

		eaPush(&lleParent->children, lle);
	}

	suiListEntryCreate(&lle->le, SAFE_MEMBER(lleParent, le), wd->leftList.lec, lle);

	suiListEntrySetHeight(lle->le, FIRST_IF_SET(height, 25));
}

static void twUpdateNotifyIcon(TempWindowWD* wd){
	if(!eaSize(&wd->net.server.lleClients)){
		suiMainWindowNotifyIconSet(wd->mw, 'P', 0xffcc88, "Profiler");
	}else{
		suiMainWindowNotifyIconSet(wd->mw, 'P', 0x88ff88, "Profiler");
	}
}

static void lleDestroyTempWindowList(LeftListEntry* lle){
	LeftListEntryProcess* p = &lle->process;

	assert(lle->lleType == LLE_PROCESS);

	if(p->twl){
		assert(p->twl->lleProcess == lle);
		p->twl->lleProcess = NULL;
		twListDestroy(lle->wdOwner, &p->twl);
	}
}

static void lleStopTimerForNetClient(	LeftListEntry* lleProcess,
										LeftListEntry* lleClient,
										LeftListEntry* lleClientToProcess);

static void twListDisconnectFromLiveProfile(TempWindowList* twl){
	if(!twl){
		return;
	}

	if(twl->atReader){
		autoTimerReaderStreamDestroy(&twl->atReaderStream);
		autoTimerReaderDestroy(&twl->atReader);

		suiButtonSetTextColor(twl->wTabButton, 0xff5555);
	}

	if(twl->lleProcess){
		LeftListEntry*			lleProcess = twl->lleProcess;
		LeftListEntryProcess*	p = &lleProcess->process;
		
		p->twl = NULL;
		twl->lleProcess = NULL;
		
		lleSendToHostStopTimer(lleProcess);

		twl->flags.processIsGone = 1;
		
		// Make the tab red to indicate the process is gone.

		suiButtonSetTextColor(twl->wTabButton, 0xff5555);
	}
}

static void lleDestroy(LeftListEntry** lleInOut);

static void lleDestroyChildren(LeftListEntry* lle){
	if(!lle){
		return;
	}

	while(lle->children){
		LeftListEntry* lleChild = lle->children[0];
		lleDestroy(&lleChild);
	}
}

static void lleDestroy(LeftListEntry** lleInOut){
	LeftListEntry*	lle = SAFE_DEREF(lleInOut);
	TempWindowWD*	wd;

	if(!lle){
		return;
	}

	if(!FALSE_THEN_SET(lle->flags.destroying)){
		return;
	}

	wd = lle->wdOwner;

	if(lle->parent){
		if(eaFindAndRemove(&lle->parent->children, lle) < 0){
			assert(0);
		}

		if(!eaSize(&lle->parent->children)){
			eaDestroy(&lle->parent->children);
		}
	}

	suiListEntryDestroy(&lle->le);

	switch(lle->lleType){
		xcase LLE_GROUP:{
			if(lle == wd->leftList.lleCommands){
				wd->leftList.lleCommands = NULL;
			}
			else if(lle == wd->leftList.lleRecordings){
				wd->leftList.lleRecordings = NULL;
			}
		}

		xcase LLE_HOST:{
			lleHostDisconnect(lle);
			
			if(lle->host.flags.isLocal){
				wd->leftList.lleHostLocal = NULL;
			}
			else if(lle->host.flags.isRemoteProcesses){
				wd->leftList.lleHostRemote = NULL;
			}else{
				if(eaFindAndRemove(&wd->leftList.lleNetServers, lle) < 0){
					assert(0);
				}
				if(!eaSize(&wd->leftList.lleNetServers)){
					eaDestroy(&wd->leftList.lleNetServers);
				}
			}

			if(lle->host.pcs){
				assert(lle->host.pcs->lle == lle);

				if(eaFindAndRemove(&wd->config.cur.servers, lle->host.pcs) < 0){
					assert(0);
				}

				StructDestroySafe(parse_ProfilerConfigServer, &lle->host.pcs);
			}
		}

		xcase LLE_NET_CLIENT_PROFILER:
		acase LLE_NET_CLIENT_PROCESS:{
			SAFE_FREE(lle->netClient.computerName);
			SAFE_FREE(lle->netClient.userName);

			if(eaFindAndRemove(&wd->net.server.lleClients, lle) < 0){
				assert(0);
			}

			if(!eaSize(&wd->net.server.lleClients)){
				eaDestroy(&wd->net.server.lleClients);
				twUpdateNotifyIcon(wd);
			}

			if(lle->netClient.link){
				linkSetUserData(lle->netClient.link, NULL);
				linkFlushAndClose(&lle->netClient.link, __FUNCTION__);
			}
			
			lleDestroyChildren(lle);

			lleDestroy(&lle->netClient.lleRemoteProcess);
		}

		xcase LLE_NET_CLIENT_TO_PROCESS:{
			LeftListEntryProcess* p = &lle->netClientToProcess.lleProcess->process;

			assert(p->flags.isLocal);

			lleStopTimerForNetClient(	lle->netClientToProcess.lleProcess,
										lle->netClientToProcess.lleClient,
										lle);

			if(eaFindAndRemove(&p->local.lleNetClients, lle) < 0){
				assert(0);
			}

			if(!eaSize(&p->local.lleNetClients)){
				eaDestroy(&p->local.lleNetClients);
			}

			eaFindAndRemove(&p->local.llesToSendBufferTo, lle);

			lleSendToClientTimerStopped(lle->netClientToProcess.lleClient,
										p->pid,
										p->pidInstance,
										lle->netClientToProcess.clientStartID);
		}

		xcase LLE_PROCESS:{
			LeftListEntryProcess* p = &lle->process;

			fbDestroy(&p->local.fb);
			
			// Disconnect from the twl, leave it in case user wants to look at it.

			twListDisconnectFromLiveProfile(p->twl);

			if(p->flags.isLocal){
				eaDestroyEx(&p->local.stringsToSend, NULL);

				pcDestroy(&p->local.pc);

				EARRAY_CONST_FOREACH_BEGIN(wd->net.server.lleClients, i, isize);
					lleSendToClientProcessRemove(	wd->net.server.lleClients[i],
													p->pid,
													p->pidInstance);
				EARRAY_FOREACH_END;

				while(p->local.lleNetClients){
					LeftListEntry* lleClientToProcess = p->local.lleNetClients[0];
					lleDestroy(&lleClientToProcess);
				}

				eaDestroy(&p->local.llesToSendBufferTo);
			}else{
				if(p->remote.lleNetClientProcess){
					p->remote.lleNetClientProcess->netClient.lleRemoteProcess = NULL;
				}
			}

			SAFE_FREE(p->exeName);
		}

		xcase LLE_RECORDINGS_FOLDER:{
			ProfilerConfigRecordingFolder* rf = lle->recordingFolder.rf;
			
			if(rf){
				rf->lle = NULL;
			}

			fcnDestroy(&lle->recordingFolder.fcn);
		}
		
		xcase LLE_NET_CLIENTS:{
			wd->leftList.lleNetClients = NULL;
		}
	}

	lleDestroyChildren(lle);

	SAFE_FREE(lle->text);
	SAFE_FREE(lle);
}

static void lleHandleMsgBuffer(	LeftListEntry* lleProcess,
								char* params)
{
	TempWindowWD*			wd = lleProcess->wdOwner;
	LeftListEntryProcess*	p = &lleProcess->process;

	assert(!p->local.fb);
	fbCreate(&p->local.fb, 1);

	p->local.bufferSizeToReceive = atoi(params);

	params = strchr(params, ' ');

	if(params){
		params++;
		p->updatesRemaining = atoi(params);

		params = strchr(params, ' ');

		if(params){
			params++;
			p->bytesRemaining = atoi(params);

			// Find all IDs.

			params = strchr(params, ' ');

			eaSetSize(&p->local.llesToSendBufferTo, 0);

			while(params){
				U32 id = atoi(++params);

				if(p->local.startID == id){
					eaPush(&p->local.llesToSendBufferTo, lleProcess);
				}else{
					EARRAY_CONST_FOREACH_BEGIN(p->local.lleNetClients, i, isize);
						LeftListEntry* lle = p->local.lleNetClients[i];

						assert(lle->lleType == LLE_NET_CLIENT_TO_PROCESS);

						if(lle->netClientToProcess.processStartID == id){
							eaPush(&p->local.llesToSendBufferTo, lle);
						}
					EARRAY_FOREACH_END;
				}

				params = strchr(params, ' ');
			}
		}

		suiWindowInvalidate(wd->leftList.w);
	}
}

static void lleHandleMsgProcessTime(LeftListEntry* lleProcess,
									char* params)
{
	TempWindowWD*			wd = lleProcess->wdOwner;
	LeftListEntryProcess*	p = &lleProcess->process;
	
	FOR_BEGIN(i, 3);
	{
		U64 value = 0;
		while(	*params >= '0' &&
				*params <= '9')
		{
			value = value * 10 + *params - '0';
			params++;
		}
		
		while(*params == ' '){
			params++;
		}
		
		switch(i){
			xcase 0: p->osTimes.cycles = value;
			xcase 1: p->osTimes.ticksUser = value;
			xcase 2: p->osTimes.ticksKernel = value;
		}
	}
	FOR_END;
	
	EARRAY_CONST_FOREACH_BEGIN(wd->net.server.lleClients, i, isize);
		lleSendToClientProcessTimes(wd->net.server.lleClients[i],
									lleProcess);
	EARRAY_FOREACH_END;

	suiWindowInvalidate(lleProcess->wdOwner->leftList.w);
}

static void lleHandleTimerStoppedWithID(LeftListEntry* lleProcess,
										char* params)
{
	TempWindowWD*			wd = lleProcess->wdOwner;
	LeftListEntryProcess*	p = &lleProcess->process;
	U32						id = atoi(params);

	//printf("Timer Stopped! (pid %d, id %d)\n", p->pid, id);

	if(id == p->local.startID){
		p->local.startID = 0;

		if(p->twl){
			assert(p->twl->lleProcess == lleProcess);
			twListDestroy(wd, &p->twl);
		}
	}else{
		EARRAY_CONST_FOREACH_BEGIN(p->local.lleNetClients, i, isize);
			LeftListEntry* lle = p->local.lleNetClients[i];

			assert(lle->lleType == LLE_NET_CLIENT_TO_PROCESS);

			if(lle->netClientToProcess.processStartID == id){
				lleDestroy(&lle);
				break;
			}
		EARRAY_FOREACH_END;
	}

	if(	!p->local.startID &&
		!p->local.lleNetClients)
	{
		p->updatesRemaining = 0;
		p->bytesRemaining = 0;
	}
}

static void twPipeClientMsgHandler(const PipeClientMsg* msg){
	LeftListEntry*			lleProcess = msg->pc.userPointer;
	LeftListEntryProcess*	p = &lleProcess->process;
	TempWindowWD*			wd = lleProcess->wdOwner;

	assert(lleProcess->lleType == LLE_PROCESS);

	switch(msg->msgType){
		xcase PC_MSG_DISCONNECT:{
			lleDestroy(&lleProcess);
		}

		xcase PC_MSG_DATA_RECEIVED:{
			if(FALSE_THEN_SET(p->flags.receivedAnything)){
				suiWindowInvalidate(wd->leftList.w);

				EARRAY_CONST_FOREACH_BEGIN(wd->net.server.lleClients, i, isize);
					lleSendToClientReceivedAnything(wd->net.server.lleClients[i],
													p->pid,
													p->pidInstance);
				EARRAY_FOREACH_END;
			}

			if(p->local.fb){
				// Currently gathering data into an fb.

				assert(msg->dataReceived.dataBytes <= p->local.bufferSizeToReceive);

				fbWriteBuffer(	p->local.fb,
								msg->dataReceived.data,
								msg->dataReceived.dataBytes);

				ADD_MISC_COUNT(msg->dataReceived.dataBytes, "bytesReceived");

				p->local.bufferSizeToReceive -= msg->dataReceived.dataBytes;

				if(!p->local.bufferSizeToReceive){
					FragmentedBuffer* fb = p->local.fb;
					
					if(PERFINFO_RUN_CONDITIONS){
						U32 byteCount;

						fbGetSizeAsBytes(fb, &byteCount);
						
						ADD_MISC_COUNT(byteCount, "bufferSizeToDecode");
					}

					EARRAY_CONST_FOREACH_BEGIN(p->local.llesToSendBufferTo, i, isize);
						LeftListEntry* lle = p->local.llesToSendBufferTo[i];

						if(lle->lleType == LLE_PROCESS){
							if(SAFE_MEMBER(p->twl, atDecoder)){
								autoTimerDecoderDecode(p->twl->atDecoder, fb);
							}

							if(p->pfw){
								pfwWriteFragmentedBuffer(p->pfw, fb);
							}
						}else{
							assert(lle->lleType == LLE_NET_CLIENT_TO_PROCESS);

							lleSendToClientFragmentedBuffer(lle,
															p->pid,
															p->pidInstance,
															fb);
						}
					EARRAY_FOREACH_END;

					fbDestroy(&p->local.fb);
				}
			}else{
				// Receiving text messages.

				char	buffer[1000];
				char*	params;

				strncpy_trunc(	buffer,
								msg->dataReceived.data,
								msg->dataReceived.dataBytes);

				//printf("Received: \"%s\"\n", buffer);

				#define CMD(x)			(!stricmp(buffer, x))
				#define CMD_PARAMS(x)	(strStartsWith(buffer, x" ") ? ((params = buffer + strlen(x" ")),1): 0)

				if(CMD_PARAMS("Buffer")){
					// Start receiving an fb.

					lleHandleMsgBuffer(lleProcess, params);
				}
				else if(CMD_PARAMS("ProcessTimes")){
					lleHandleMsgProcessTime(lleProcess, params);
				}
				else if(CMD_PARAMS("TimerStoppedWithID")){
					lleHandleTimerStoppedWithID(lleProcess, params);
				}
				else if(CMD("ThreadsAreActive")){
					p->flags.threadsAreActive = 1;
					suiWindowInvalidate(wd->leftList.w);

					EARRAY_CONST_FOREACH_BEGIN(wd->net.server.lleClients, i, isize);
						lleSendToClientThreadsAreActive(wd->net.server.lleClients[i],
														p->pid,
														p->pidInstance,
														1);
					EARRAY_FOREACH_END;
				}
				else if(CMD("NoThreadsAreActive")){
					p->flags.threadsAreActive = 0;
					suiWindowInvalidate(wd->leftList.w);

					EARRAY_CONST_FOREACH_BEGIN(wd->net.server.lleClients, i, isize);
						lleSendToClientThreadsAreActive(wd->net.server.lleClients[i],
														p->pid,
														p->pidInstance,
														0);
					EARRAY_FOREACH_END;
				}

				#undef CMD
				#undef CMD_PARAMS
			}

			//printf("From server: \"%s\".\n", buffer);
		}
		
		xcase PC_MSG_DATA_SENT:{
			while(eaSize(&p->local.stringsToSend)){
				char* text = p->local.stringsToSend[0];
				
				if(!pcWriteString(p->local.pc, text, NULL)){
					break;
				}
				
				free(text);
				
				eaRemove(&p->local.stringsToSend, 0);
			}
		}
	}
}

static void twConnectToLocalProcess(TempWindowWD* wd,
									U32 pid,
									const char* exeName,
									S32 startTimer)
{
	FOR_BEGIN(i, 100);
		char			serverPipeName[100];
		LeftListEntry*	lle;
		S32				alreadyExists = 0;

		sprintf(serverPipeName, "CrypticProfiler%d", pid);

		if(i){
			strcatf(serverPipeName, ".%d", i);
		}

		if(!pcServerIsAvailable(serverPipeName)){
			break;;
		}

		EARRAY_CONST_FOREACH_BEGIN(wd->leftList.lleHostLocal->children, j, jsize);
			LeftListEntry* lleCur = wd->leftList.lleHostLocal->children[j];

			assert(lleCur->lleType == LLE_PROCESS);

			if(	lleCur->process.pid == pid &&
				lleCur->process.pidInstance == i)
			{
				alreadyExists = 1;
				break;
			}
		EARRAY_FOREACH_END;

		if(alreadyExists){
			continue;
		}

		lleCreate(	&lle,
					wd,
					wd->leftList.lleHostLocal,
					LLE_PROCESS,
					NULL,
					18);

		lle->process.flags.isLocal = 1;
		lle->process.pid = pid;
		lle->process.pidInstance = i;
		lle->process.exeName = strdup(exeName);

		if(!pcCreate(&lle->process.local.pc, lle, twPipeClientMsgHandler, wd->iocp)){
			printf("Failed to create PipeClient.\n");
		}
		else if(!pcConnect(lle->process.local.pc, serverPipeName, 1000)){
			printf("Failed to connect to PipeServer \"%s\".\n", serverPipeName);
		}

		if(!pcIsConnected(lle->process.local.pc)){
			lleDestroy(&lle);
		}else{
			printf("Connected to process pipe \"%s\".\n", serverPipeName);

			EARRAY_CONST_FOREACH_BEGIN(wd->net.server.lleClients, j, jsize);
				lleSendToClientProcessAdd(wd->net.server.lleClients[j], lle);
			EARRAY_FOREACH_END;
		}
		
		if(	startTimer &&
			wd->flags.profileNewProcesses)
		{
			lleSendToHostStartTimer(lle);
		}
	FOR_END;
}

static void twPipeServerConnectAtStartupMsgHandler(const PipeServerMsg* msg){
	TempWindowWD* wd = msg->ps.userPointer;

	switch(msg->msgType){
		xcase PS_MSG_CLIENT_CONNECT:{
		}

		xcase PS_MSG_CLIENT_DISCONNECT:{
		}

		xcase PS_MSG_DATA_RECEIVED:{
			const char* tag = "ConnectToMe ";
			char		buffer[1000];

			strncpy(buffer, msg->dataReceived.data, msg->dataReceived.dataBytes);
			buffer[msg->dataReceived.dataBytes] = 0;

			printf("Process connecting at startup: \"%s\"\n", buffer);

			if(strStartsWith(buffer, tag)){
				U32		pid = atoi(buffer + strlen(tag));
				char*	token;

				token = strchr(buffer + strlen(tag), ' ');

				if(token){
					token++;
				}

				if(pid){
					twConnectToLocalProcess(wd, pid, token, 1);
				}

				psClientDisconnect(msg->psc.psc);
			}
		}
	}
}

static void twleDrawColumns(const SUIListMsgEntryDraw* md,
							U64 cycles,
							U32 count,
							U32 frames,
							U32 depth,
							U32 subTimerCount)
{
	char	buffer[100];
	S32		x = 400 - md->xIndent;

	sprintf(buffer,
			"%s",
			getCommaSeparatedU64(cycles));

	printWithShadow(md->dc, x += 80, 2, buffer, 15, 0xffaaffaa);

	if(	!count)
	{
		x += 120;
	}else{
	
		sprintf(buffer,
					"%s",
					getCommaSeparatedU64(cycles / count));

		printWithShadow(md->dc, x += 120, 2, buffer, 15, 0xffaaffaa);
	}

	if(	!frames)
	{
		x += 120 + 80;
	}else{
		if (!count)
		{
			x += 120;
		}
		else
		{
			sprintf(buffer,
					"%s / %s",
					getCommaSeparatedU64(count / frames),
					getCommaSeparatedU64(frames));

			printWithShadow(md->dc, x += 120, 2, buffer, 15, 0xffaaffaa);
		}

		sprintf(buffer,
				"%s",
				getCommaSeparatedU64(cycles / frames));

		printWithShadow(md->dc, x += 80, 2, buffer, 15, 0xffaaffaa);
	}

	buffer[0] = 0;
	
	if(depth){
		if(subTimerCount){
			sprintf(buffer,
					"%d / %d",
					depth,
					subTimerCount);
		}else{
			sprintf(buffer,
					"%d",
					depth);
		}
	}
	else if(subTimerCount){
		sprintf(buffer,
				"%d",
				subTimerCount);
	}

	x += 120;

	if(buffer[0]){
		printWithShadow(md->dc, x, 2, buffer, 15, 0xffaaffaa);
	}
}

static void twListDrawHistoryGraph(	const SUIWindowMsg* msg,
									const TempWindowListEntry* twle,
									const TimerHistoryChunkList* cl,
									S32 showWholeGraph,
									const TempWindowListEntry* twleThread,
									const GraphPosition* gp,
									ShownDataType shownData,
									S32 showOtherValue)
{
	const SUIListMsgEntryDraw*		md = msg->msgData;
	const TempWindowList*			twl = msg->pipe.userPointer;
	const TimerHistoryChunkList*	clFrames;
	U32								argbCycles = shownData == SHOWN_DATA_HIT_COUNT ?
													ARGB_HIT_COUNT :
													showOtherValue ?
														ARGB_NON_CYCLES :
														ARGB_CYCLES_ACTIVE;

	if(gp->sx <= 0){
		return;
	}

	assert(twleThread->twleType == TWLE_THREAD);
	
	twleGetFramesHistory(twle, &clFrames);

	drawHistoryGraph(	md->dc,
						shownData,
						gp->x - gp->xIndent,
						gp->y,
						gp->sx,
						gp->sy,
						cl,
						clFrames,
						showWholeGraph ?
							twl->cyclesMinReceived :
							twl->cyclesMax - twl->cyclesViewedDelta,
						showWholeGraph ?
							twl->cyclesMaxReceived :
							twl->cyclesMax,
						shownData == SHOWN_DATA_HIT_COUNT ?
							twl->scaleCount :
							twl->scaleCycles * 3 * SQR((U64)1000),
						twleThread->thread.locked.frameIndex ?
							twleThread->thread.locked.cyclesBegin :
							twl->selectedCyclesBegin,
						twleThread->thread.locked.frameIndex ?
							twleThread->thread.locked.cyclesDelta :
							twl->selectedCyclesDelta,
						!!twleThread->thread.locked.frameIndex,
						showOtherValue,
						argbCycles,
						twle == twl->twleUnderMouse ?
							0xff333322 :
							twle->flags.childIsUnderMouse ?
								SAFE_MEMBER(twle->parent, flags.childUnderMouseIsMine) ?
									0xff222255 :
									0xff222233 :
								0);
}

static S32 twleGetGraphPos(	const TempWindowListEntry* twle,
							S32 sx,
							S32 xIndent,
							GraphPosition* gpOut)
{
	if(	!twle ||
		twle->twleType == TWLE_GROUP)
	{
		return 0;
	}

	gpOut->xIndent = xIndent;

	if(twle->flags.showEntireHistory){
		gpOut->x = xIndent;
		gpOut->y = 20;
		gpOut->sx = sx - 20;
		suiListEntryGetHeight(twle->le, &gpOut->sy);
		gpOut->sy -= gpOut->y;
	}else{
		gpOut->x = 1000;
		gpOut->y = 0;
		gpOut->sx = xIndent + sx - gpOut->x - 20;
		suiListEntryGetHeight(twle->le, &gpOut->sy);
		gpOut->sy -= gpOut->y;
	}
	
	return 1;
}

static S32 twleGetHistory(	TempWindowListEntry* twle,
							TimerHistoryChunkList** clCyclesOut)
{
	TimerHistoryChunkList*	clCycles = NULL;

	if(twle->twleType == TWLE_COPY){
		twle = twle->copy.twleOriginal;
	}

	switch(twle->twleType){
		xcase TWLE_THREAD:{
			clCycles = &twle->thread.history;
		}
		
		xcase TWLE_SCAN_FRAMES:{
			clCycles = &twle->twleThread->thread.scanHistory;
		}

		xcase TWLE_OS_GROUP:{
			clCycles = &twle->twleThread->thread.history;
		}
		
		xcase TWLE_OS_CYCLES:{
			clCycles = &twle->twleThread->thread.history;
		}

		xcase TWLE_OS_TICKS_USER:{
			clCycles = &twle->twleThread->thread.history;
		}

		xcase TWLE_OS_TICKS_KERNEL:{
			clCycles = &twle->twleThread->thread.history;
		}

		xcase TWLE_TIMER_INSTANCE:{
			clCycles = &twle->timerInstance.history;
		}

		xcase TWLE_TIMER_ID:{
			clCycles = &twle->timerID.history;
		}

		xcase TWLE_TIMER_ID_GROUP:{
			clCycles = &twle->twleThread->thread.history;
		}
	}

	if(clCyclesOut){
		*clCyclesOut = clCycles;
	}

	return !!clCycles;
}

static S32 twleGetHistoryConst(	const TempWindowListEntry* twle,
								const TimerHistoryChunkList** clCyclesOut)
{
	return twleGetHistory(	(TempWindowListEntry*)twle,
							(TimerHistoryChunkList**)clCyclesOut);
}

static void twlUpdateSelectedFrame(	TempWindowList* twl,
									TempWindowListEntry* twle,
									S32 x,
									const GraphPosition* gp,
									U32* frameIndexOut)
{
	TimerHistoryChunkList* cl;

	if(twleGetFramesHistory(twle, &cl)){
		findSelectedHistoryFrame(	frameIndexOut,
									&twl->selectedCyclesBegin,
									&twl->selectedCyclesDelta,
									x - gp->x,
									gp->sx,
									cl,
									cl,
									twle->flags.showEntireHistory ?
										twl->cyclesMinReceived :
										twl->cyclesMax - twl->cyclesViewedDelta,
									twle->flags.showEntireHistory ?
										twl->cyclesMaxReceived :
										twl->cyclesMax);
	}
}

static void twleSetChildIsUnderMouse(	TempWindowListEntry* twle,
										S32 set,
										S32 isMyChild)
{
	set = !!set;
	isMyChild = set ? !!isMyChild : 0;

	while(twle){
		twle->flags.childIsUnderMouse = set;
		twle->flags.childUnderMouseIsMine = isMyChild;
		isMyChild = 0;
		twle = twle->parent;
	}
}

static S32 twleHandleMouseEnter(SUIWindow* w,
								TempWindowWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIListMsgEntryMouseEnter*	md = msg->msgData;
	TempWindowList*						twl = msg->pipe.userPointer;
	TempWindowListEntry*				twle = md->le.userPointer;

	if(	!twl->redrawInterval ||
		twl->redrawInterval > 50)
	{
		twl->redrawInterval = 50;
	}

	twleSetChildIsUnderMouse(SAFE_MEMBER(twl->twleUnderMouse, parent), 0, 0);
	twl->twleUnderMouse = twle;
	twleSetChildIsUnderMouse(SAFE_MEMBER(twl->twleUnderMouse, parent), 1, 1);

	if(twle){
		switch(twle->twleType){
			xcase TWLE_TIMER_INSTANCE:{
				twle->timerInstance.flags.selectedFromCopy = 0;
			}
			xcase TWLE_COPY:{
				twle->copy.flags.selectedFromOriginal = 0;
			}
		}
	}

	return 1;
}

static S32 twleHandleMouseLeave(SUIWindow* w,
								TempWindowWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIListMsgEntryMouseLeave*	md = msg->msgData;
	TempWindowList*						twl = msg->pipe.userPointer;

	if(twl->twleUnderMouse){
		twleSetChildIsUnderMouse(twl->twleUnderMouse->parent, 0, 0);
		twl->twleUnderMouse = NULL;
	}

	if(md->flags.enteredEmptySpace){
		if(!twl->dragAnchorCycles){
			twl->selectedCyclesBegin = 0;
			twl->selectedCyclesDelta = 0;

			detailViewSetTarget(twl->wDetailView,
								-300,
								0,
								NULL,
								NULL,
								NULL,
								0,
								0);
		}
	}

	if(	!twl->redrawInterval ||
		twl->redrawInterval > 50)
	{
		twl->redrawInterval = 50;
	}

	return 1;
}

static S32 twleHandleMouseMove(	SUIWindow* w,
								TempWindowWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIListMsgEntryMouse*			md = msg->msgData;
	TempWindowList*						twl = msg->pipe.userPointer;
	TempWindowListEntry*				twle = md->le.userPointer;
	S32									x = md->xIndent + md->x;
	TempWindowListEntry*				twleThread = NULL;
	U32									selectedFrameIndex = 0;
	GraphPosition						gp;
	S32									hasGraphPos;

	if(!twle){
		return 1;
	}

	hasGraphPos = twleGetGraphPos(twle, md->sx, md->xIndent, &gp);

	if(!hasGraphPos){
		return 1;
	}
	
	if(twl->dragAnchorCycles){
		U64 curCycles;

		assert(gp.sx);

		curCycles = twl->cyclesMax -
					twl->cyclesViewedDelta +
					(x - gp.x) * (S64)twl->cyclesViewedDelta / gp.sx;

		if(!twl->cyclesMaxLocked){
			twl->cyclesMaxLocked = twl->cyclesMaxReceived;
		}

		twl->cyclesMaxLocked +=	twl->dragAnchorCycles -
								curCycles;

		if(twl->cyclesMaxLocked - twl->cyclesViewedDelta < twl->cyclesMinReceived){
			twl->cyclesMaxLocked = twl->cyclesMinReceived + twl->cyclesViewedDelta;
		}

		MIN1(twl->cyclesMaxLocked, twl->cyclesMaxReceived);
		twl->cyclesMax = twl->cyclesMaxLocked;

		if(twl->cyclesMax == twl->cyclesMaxReceived){
			twl->cyclesMaxLocked = 0;
		}
	}else{
		TimerHistoryChunkList*	clCycles = NULL;
		S32						isOverGraph = 0;
		S32						isOverGraphX =	x >= gp.x &&
												x < gp.x + gp.sx;

		twl->selectedCyclesBegin = 0;
		twl->selectedCyclesDelta = 0;

		if(isOverGraphX){
			isOverGraph = 1;

			twleGetThread(twle, &twleThread);

			twleGetHistory(	twle,
							&clCycles);

			if(SAFE_MEMBER(twleThread, thread.locked.frameIndex)){
				selectedFrameIndex = twleThread->thread.locked.frameIndex;
				twl->selectedCyclesBegin = twleThread->thread.locked.cyclesBegin;
				twl->selectedCyclesDelta = twleThread->thread.locked.cyclesDelta;
			}else{
				twlUpdateSelectedFrame(	twl,
										twle,
										x,
										&gp,
										&selectedFrameIndex);
			}
		}

		if(	isOverGraphX
			||
			twl->selectedCyclesDelta &&
			!twle->flags.showEntireHistory)
		{
			S32								yTarget = 50;
			IVec2							posMouseList;
			S32								showOtherValue = 0;
			TempWindowListEntry*			twleTimerID = NULL;
			ShownDataType					shownData = wd->shownData;
			const TimerHistoryChunkList*	clFrames;
			
			if(twle->twleType == TWLE_TIMER_INSTANCE){
				twleTimerID = twle->timerInstance.twleTimerID;
			}
			else if(twle->twleType == TWLE_COPY){
				twleTimerID = twle->copy.twleOriginal->timerInstance.twleTimerID;
			}
			else if(twle->twleType == TWLE_TIMER_ID){
				twleTimerID = twle;
			}
			
			switch(twle->twleType){
				xcase TWLE_THREAD:{
					if(twl->flags.hasOSCycles){
						shownData = SHOWN_DATA_CPU_ACTIVE;
						showOtherValue = 2;
					}
				}
				
				xcase TWLE_SCAN_FRAMES:{
					shownData = SHOWN_DATA_CPU_ACTIVE;
					showOtherValue = 2;
				}

				xcase TWLE_OS_CYCLES:
				acase TWLE_OS_GROUP:{
					shownData = SHOWN_DATA_CPU_ACTIVE;
					showOtherValue = 2;
				}
				
				xdefault:{
					if(	!wd->flags.showCyclesOnly &&
						twleTimerID &&
						twleTimerID->timerID.timerType != PERFINFO_TYPE_CPU)
					{
						showOtherValue = 1;
					}
				}
			}

			if(suiWindowGetMousePos(twl->wList, posMouseList)){
				S32 syList = suiWindowGetSizeY(twl->wList);

				if(posMouseList[1] < syList / 2){
					yTarget = syList - suiWindowGetSizeY(twl->wDetailView) - 50;
				}
			}
			
			twleGetFramesHistory(twle, &clFrames);

			detailViewSetTarget(twl->wDetailView,
								yTarget,
								selectedFrameIndex,
								twle->twleType == TWLE_COPY ?
									twle->copy.twleOriginal->text :
									twle->text,
								clCycles->count ?
									clCycles :
									NULL,
								clFrames,
								shownData,
								showOtherValue);
		}else{
			detailViewSetTarget(twl->wDetailView,
								-300,
								0,
								NULL,
								NULL,
								NULL,
								0,
								0);
		}
	}

	if(	!twl->redrawInterval ||
		twl->redrawInterval > 50)
	{
		twl->redrawInterval = 50;
	}

	return 1;
}

static S32 twListIsLiveProcess(const TempWindowList* twl){
	return 	twl->atReader ||
			twl->lleProcess;
}

static void printPercent(	SUIWindow* w,
							const SUIDrawContext* dc,
							F32 percent,
							S32 x,
							S32 y,
							S32* sizeXOut)
{
	static const U32 colorScale[][2] =	{	{0, 0x555555},
											{1, 0x9999ff},
											{5, 0x99ff99},
											{10, 0xffff99},
											{100, 0xffff9999}};

	S32			useSmallText;
	U32			height;
	char*		dot;
	S32			sx;
	U32			argb = colorScale[ARRAY_SIZE(colorScale) - 1][1];
	char		buffer[20];
	const S32	minSize = 20;

	useSmallText = percent < 1.f;
	height = useSmallText ? 12 : 15;

	ARRAY_FOREACH_BEGIN(colorScale, i);
		if(percent <= colorScale[i][0]){
			if(i){
				U32 scale = 0xff *
							#pragma warning(suppress:6200) // VS2010_sal
							(percent - (F32)colorScale[i - 1][0]) /
							#pragma warning(suppress:6200) // VS2010_sal
							((F32)colorScale[i][0] - (F32)colorScale[i - 1][0]);

				MINMAX1(scale, 0, 0xff);

				argb = suiColorInterpAllRGB(0xff,
											#pragma warning(suppress:6200) // VS2010_sal
											colorScale[i - 1][1],
											colorScale[i][1],
											scale);

				break;
			}else{
				argb = colorScale[i][1];
			}
		}
	ARRAY_FOREACH_END;

	sprintf(buffer,
			FORMAT_OK(	percent >= 99.95f ?
							"%.2f" :
							percent >= 9.995f ?
								"%.2f" :
								"%.2f"),
			percent);

	for(dot = buffer; *dot && *dot != '.'; dot++);
	
	if(*dot == '.'){
		*dot = 0;

		while(1){
			suiWindowGetTextSize(w, buffer, height, &sx, NULL);

			if(sizeXOut){
				*sizeXOut = MAX(sx, minSize) + 15;
			}
			
			if(sx <= minSize){
				break;
			}

			if(FALSE_THEN_SET(useSmallText)){
				height = 12;
			}else{
				x += sx - minSize;
				break;
			}
		}

		printWithShadow(dc,
						x + minSize - sx,
						y + (useSmallText ? 2 : 0),
						buffer,
						height,
						argb);

		*dot = '.';
	}

	//argb = suiColorInterpAllRGB(0xff, argb, 0, 128);

	printWithShadow(dc,
					x + minSize,
					y + 2,
					dot,
					12,
					argb);
}

static S32 twleHandleDraw(	SUIWindow* w,
							TempWindowWD* wd,
							const SUIWindowMsg* msg)
{
	const SUIListMsgEntryDraw*	md = msg->msgData;
	const TempWindowList*		twl = msg->pipe.userPointer;
	const TempWindowListEntry*	twle = md->le.userPointer;
	const TempWindowListEntry*	twleOriginal = twle;
	GraphPosition				gp;
	const S32					showEntireHistory = twle->flags.showEntireHistory;
	S32							selectedFromPair = 0;
	S32							childIsUnderMouse = twle->flags.childIsUnderMouse;
	S32							siblingIsUnderMouse = SAFE_MEMBER(	twle->parent,
																	flags.childUnderMouseIsMine);
	S32							xOffset = 0;
	S32							hasGraphPos;
	TimerHistoryFrame			hfCycles = {0};
	TimerHistoryFrame			hfFrame = {0};

	PERFINFO_AUTO_START_FUNC();

	hasGraphPos = twleGetGraphPos(twle, md->sx, md->xIndent, &gp);
	
	switch(twle->twleType){
		xcase TWLE_COPY:{
			selectedFromPair = twle->copy.flags.selectedFromOriginal;
			twle = twle->copy.twleOriginal;
		}
		
		xcase TWLE_TIMER_INSTANCE:{
			selectedFromPair = twle->timerInstance.flags.selectedFromCopy;
		}
		
		xcase TWLE_THREAD:{
			U32 argb =	twle->thread.flags.destroyed ?
							twle->thread.flags.hasVisibleTimerIDs ?
								0xff662222 :
								0xff332222 :
							twle->thread.flags.hasVisibleTimerIDs ?
								0xff224422 :
								0xff112211;

			if(md->flags.isUnderMouse){
				argb = suiColorInterpAllRGB(0xff, argb, 0xffffff, 64);
			}
			//else if(childIsUnderMouse){
			//	argb = suiColorInterpAllRGB(0xff, argb, 0xffff00, 50);
			//}

			suiDrawFilledRect(	md->dc,
								0,
								0,
								md->sx,
								md->sy,
								argb);
		}
	}

	if(childIsUnderMouse){
		U32 rgbHighlight = 0x8888ff;

		suiDrawFilledRect(	md->dc,
							0,
							md->sy - 1,
							md->sx,
							1,
							suiColorInterpAllRGB(0xff, md->argbDefault, rgbHighlight, 150));

		suiDrawFilledRect(	md->dc,
							0,
							md->sy - 2,
							md->sx,
							1,
							suiColorInterpAllRGB(0xff, md->argbDefault, rgbHighlight, 75));
	}
	else if(!md->flags.isUnderMouse &&
			siblingIsUnderMouse &&
			twle->twleType != TWLE_TIMER_ID &&
			twle->twleType != TWLE_COPY)
	{
		// Highlight siblings.

		#if 0
		{
			U32 rgbHighlight = 0xffff88;

			suiDrawFilledRect(	md->dc,
								0,
								0,
								md->sx,
								md->sy,
								suiColorInterpAllRGB(0xff, md->argbDefault, rgbHighlight, 10));
		}
		#endif
	}

	if(	hasGraphPos &&
		md->sx + md->xIndent > 1020 &&
		md->flags.isUnderMouse &&
		twl->cyclesMaxLocked)
	{
		// Draw the "go to end" button.

		S32 bx = md->sx - BUTTON_GO_TO_END_OFFSET;

		suiDrawFilledRect(	md->dc,
							bx,
							2,
							15,
							15,
							0xffff0000);

		suiDrawRect(md->dc,
					bx,
					2,
					15,
					15,
					1,
					0xff000000);

		suiDrawFilledTriangle(	md->dc,
								bx + 2,
								2 + 2,
								bx + 15 - 2,
								2 + 15 / 2,
								bx + 2,
								2 + 15 - 2,
								0xff000000);
	}

	if(	hasGraphPos
		&&
		(	md->flags.isUnderMouse ||
			showEntireHistory))
	{
		// Draw the "toggle entire history" offset.

		S32 bx = 1000 - BUTTON_ENTIRE_HISTORY_OFFSET - md->xIndent;

		suiDrawFilledRect(	md->dc,
							bx,
							2,
							15,
							15,
							0xff0000ff);

		suiDrawRect(md->dc,
					bx,
					2,
					15,
					15,
					1,
					0xff000000);

		if(showEntireHistory){
			suiDrawFilledTriangle(	md->dc,
									bx + 2, 2 + 15 - 2,
									bx + 15 - 2, 2 + 15 - 2,
									bx + 15 / 2, 2 + 3,
									0xff8888ff);
		}else{
			suiDrawFilledTriangle(	md->dc,
									bx + 2, 2 + 3,
									bx + 15 - 2, 2 + 3,
									bx + 15 / 2, 15 - 2,
									0xff000000);
		}
	}

	if(	twListIsLiveProcess(twl) &&
		twle->twleType == TWLE_TIMER_INSTANCE &&
		twle->timerInstance.twleTimerID)
	{
		if(	md->flags.isUnderMouse &&
			wd->flags.breakPointsEnabled
			||
			twle->timerInstance.flags.isBreakpoint)
		{
			// Draw the "breakpoint toggle" button.

			suiDrawFilledRect(	md->dc,
								1000 - BUTTON_BREAKPOINT_OFFSET - md->xIndent,
								2,
								15,
								15,
								0xffff0000);

			suiDrawRect(md->dc,
						1000 - BUTTON_BREAKPOINT_OFFSET - md->xIndent,
						2,
						15,
						15,
						1,
						0xff000000);
		}

		if(	md->flags.isUnderMouse ||
			twle->timerInstance.flags.forcedClosed)
		{
			S32 isClosed =	!twle->timerInstance.flags.forcedOpen
							&&
							(	twle->timerInstance.flags.forcedClosed ||
								twle->timerInstance.flags.closedByDepth);

			suiDrawFilledRect(	md->dc,
								1000 - BUTTON_DISABLE_TIMER_OFFSET - md->xIndent,
								2,
								15,
								15,
								isClosed ?
									0xff88ff88 :
									0xffff8888);

			suiDrawRect(md->dc,
						1000 - BUTTON_DISABLE_TIMER_OFFSET - md->xIndent,
						2,
						15,
						15,
						1,
						0xff000000);

			suiDrawFilledRect(	md->dc,
								1000 - BUTTON_DISABLE_TIMER_OFFSET - md->xIndent + 3,
								2 + 15 / 2 - 1,
								15 - 3 * 2,
								3,
								0xff000000);

			if(isClosed){
				suiDrawFilledRect(	md->dc,
									1000 - BUTTON_DISABLE_TIMER_OFFSET - md->xIndent + 15 / 2 - 1,
									2 + 3,
									3,
									15 - 3 * 2,
									0xff000000);
			}
		}
	}

	{
		// Print the name.

		U32		argb = 0xffcccccc;
		char	text[1000] = "";

		if(	twle->twleType == TWLE_TIMER_INSTANCE ||
			twle->twleType == TWLE_TIMER_ID)
		{
			const TempWindowListEntry*	twleThread;
			U64							cyclesTotal;
			U64							cycles;
			U64							parentCycles;
			F32							percent;
			U32							percentSizeX;
			TempWindowListEntry*		twleTimerID = twle->twleType == TWLE_TIMER_INSTANCE ?
															twle->timerInstance.twleTimerID :
															twle;
			const CycleCounts*			cycleCounts = twle->twleType == TWLE_TIMER_INSTANCE ?
															&twle->timerInstance.cycles :
															&twle->timerID.cycles;
			const CycleCounts*			parentCounts = twle->twleType == TWLE_TIMER_INSTANCE ?
															&twle->parent->timerInstance.cycles :
															&twle->parent->timerID.cycles;
			CycleCounts					cycleCountsFrame;
			
			twleGetThreadConst(twle, &twleThread);

			if(twleThread){
				if(twleThread->thread.locked.frameIndex){
					const TimerHistoryChunkList* clCycles;

					timerHistoryGetExistingFrame(	&twleThread->thread.history,
													twleThread->thread.locked.frameIndex,
													&hfFrame);
					
					cyclesTotal =	hfFrame.cycles.active +
									hfFrame.cycles.blocking;

					twleGetHistoryConst(twle, &clCycles);

					timerHistoryGetExistingFrame(	clCycles,
													twleThread->thread.locked.frameIndex,
													&hfCycles);

					cycleCounts = &cycleCountsFrame;
					
					cycleCountsFrame.active = hfCycles.cycles.active;
					cycleCountsFrame.blocking = hfCycles.cycles.blocking;
					cycleCountsFrame.total =	cycleCountsFrame.active +
												cycleCountsFrame.blocking;
					cycleCountsFrame.other = hfCycles.cycles.other;
				}else{
					cyclesTotal = twleThread->thread.cycles.total;
				}
			}

			if(	wd->flags.showCyclesOnly ||
				!twleTimerID ||
				twleTimerID->timerID.timerType == PERFINFO_TYPE_CPU)
			{
				switch(wd->shownData){
					xcase SHOWN_DATA_CPU_ACTIVE_THEN_BLOCKING:
					acase SHOWN_DATA_CPU_BLOCKING_THEN_ACTIVE:
						cycles = cycleCounts->total;
						parentCycles = parentCounts->total;
					xcase SHOWN_DATA_CPU_ACTIVE:
						cycles = cycleCounts->active;
						parentCycles = parentCounts->active;
					xcase SHOWN_DATA_CPU_BLOCKING:
						cycles = cycleCounts->blocking;
						parentCycles = parentCounts->blocking;
					xcase SHOWN_DATA_HIT_COUNT:
						cycles = twle->twleType == TWLE_TIMER_INSTANCE ?
									twle->timerInstance.count.hits :
									twle->timerID.count.hits;
						parentCycles = twle->twleType == TWLE_TIMER_INSTANCE ?
									twle->parent->timerInstance.count.hits :
									twle->parent->timerID.count.hits;
					xdefault:
						assert(0);
				}
			}else{
				switch(wd->shownData){
					xcase SHOWN_DATA_CPU_ACTIVE_THEN_BLOCKING:
					acase SHOWN_DATA_CPU_BLOCKING_THEN_ACTIVE:
						cycles = cycleCounts->other;
						parentCycles = cycleCounts->other;
					xcase SHOWN_DATA_CPU_ACTIVE:
						cycles = cycleCounts->active;
						parentCycles  = parentCounts->active;
					xcase SHOWN_DATA_CPU_BLOCKING:
						cycles = cycleCounts->blocking;
						parentCycles = parentCounts->blocking;
					xcase SHOWN_DATA_HIT_COUNT:
						cycles = twle->twleType == TWLE_TIMER_INSTANCE ?
									twle->timerInstance.count.hits :
									twle->timerID.count.hits;
						parentCycles = twle->twleType == TWLE_TIMER_INSTANCE ?
									twle->parent->timerInstance.count.hits :
									twle->parent->timerID.count.hits;
					xdefault:
						assert(0);
				}
			}

			if (twleShowPercentages)
			{
				percent = parentCycles 
					?  (F32)cycles * 100.f / (F32)parentCycles
					: 0.f;
			}
			else
			{
				percent = cyclesTotal 
					? (F32)cycles * 100.f / (F32)cyclesTotal
					: 0.f;
			}
			
			printPercent(w, md->dc, percent, 5, 2, &percentSizeX);
			
			xOffset += percentSizeX;
		}
		
		if(twle->twleType == TWLE_TIMER_ID){
			xOffset += 30;
		}

		// Set a prefix.

		if(	twle->twleType == TWLE_TIMER_INSTANCE &&
			twle->timerInstance.twleTimerID
			||
			twle->twleType == TWLE_TIMER_ID)
		{
			U32 timerType = twle->twleType == TWLE_TIMER_INSTANCE ?
								twle->timerInstance.twleTimerID->timerID.timerType :
								twle->timerID.timerType;

			switch(timerType){
				xcase PERFINFO_TYPE_BITS:{
					strcat(text, "bits:");
				}
				xcase PERFINFO_TYPE_MISC:{
					strcat(text, "count:");
				}
			}
		}

		if(twle->text){
			strcat(text, twle->text);
		}

		// Change text color.

		if(md->flags.isUnderMouse){
			argb = 0xffffff00;

			if(twle->twleType == TWLE_TIMER_INSTANCE){
				if(twle->timerInstance.flags.parentNotOpen){
					argb = 0xff777777;
				}
				else if(!twle->timerInstance.flags.forcedOpen){
					argb = 0xffaaaa00;
				}
			}
		}
		else if(twle->twleType == TWLE_TIMER_INSTANCE){
			if(	twle->timerInstance.flags.parentNotOpen ||
				!twle->timerInstance.count.hits)
			{
				argb = 0xff555555;
			}
			else if(twle->timerInstance.flags.forcedOpen){
				argb = 0xffaaffaa;
			}
			else if(twle->timerInstance.flags.forcedClosed ||
					twle->timerInstance.flags.closedByDepth)
			{
				argb = 0xffffaaaa;
			}
		}
		else if(twle->twleType == TWLE_THREAD){
			if(twle->thread.flags.destroyed){
				argb = 0xffffaaaa;
			}
			else if(twle->thread.flags.receivedAnUpdate){
				argb = 0xffaaaaff;
			}else{
				argb = 0xff666666;
			}
		}

		if(twle->twleType == TWLE_TIMER_INSTANCE){
			if(twle->timerInstance.flags.isBlocking){
				strcat(text, "(b)");
			}
			if(twle->timerInstance.flags.isBreakpoint){
				argb = 0xffff0000;
				strcat(text, "(BREAK)");
			}
			if(selectedFromPair){
				argb = 0xffff8000;
				strcat(text, " <---------------");
			}
		}
		else if(twle->twleType == TWLE_THREAD){
			U64 cycles = twl->flags.hasOSCycles ?
							twle->thread.os.cyclesTotal :
							twle->thread.cycles.active;
							
			F32 percent =	twle->thread.cycles.total ?
								(F32)cycles * 100.f /
									(F32)twle->thread.cycles.total :
								0.f;
			S32 sx;
			
			printPercent(w, md->dc, percent, 5, 2, &sx);
			
			xOffset += sx;

			if(twle->thread.byteCountReceived){
				strcatf(text,
						" (%s bytes)",
						getCommaSeparatedU64(twle->thread.byteCountReceived));
			}
			
			if(twle->thread.byteCountChunks){
				strcatf(text,
						" (%s bytes)",
						getCommaSeparatedU64(twle->thread.byteCountChunks));
			}

			if(twle->thread.decoderErrorText){
				strcatf(text,
						" (error: %s)",
						twle->thread.decoderErrorText);
			}
		}
		else if(twle->twleType == TWLE_OS_CYCLES
				||
				twle->twleType == TWLE_OS_GROUP &&
				twl->flags.hasOSCycles)
		{
			F32 percent =	twle->twleThread->thread.cycles.total ?
								(F32)twle->twleThread->thread.os.cyclesTotal * 100.f /
									(F32)twle->twleThread->thread.cycles.total :
								0.f;
			S32 sx;
			
			printPercent(w, md->dc, percent, 5, 2, &sx);
			
			xOffset += sx;
		}
		else if(twle->twleType == TWLE_OS_TICKS_USER){
			F32 percent =	twle->twleThread->thread.cycles.total ?
								(F32)twle->twleThread->thread.os.ticksTotal.user * 100.f /
									(F32)twle->twleThread->thread.cycles.total :
								0.f;
			S32 sx;
			
			printPercent(w, md->dc, percent, 5, 2, &sx);
			
			xOffset += sx;
		}
		else if(twle->twleType == TWLE_OS_TICKS_KERNEL){
			F32 percent =	twle->twleThread->thread.cycles.total ?
								(F32)twle->twleThread->thread.os.ticksTotal.kernel * 100.f /
									(F32)twle->twleThread->thread.cycles.total :
								0.f;
			S32 sx;
			
			printPercent(w, md->dc, percent, 5, 2, &sx);
			
			xOffset += sx;
		}
		else if(twle->twleType == TWLE_TIMER_ID){
			if(twle->timerID.flags.hasRecursion){
				strcatf(text,
						" (recursive)");
			}
		}
		else if(twle->twleType == TWLE_TIMER_ID_GROUP){
			strcatf(text,
					" (%d)",
					twle->timerIDGroup.timerInstanceCount);
					
			if(SAFE_DEREF(twl->nameFilter)){
				strcatf(text,
						" (Filter: \"%s\")",
						twl->nameFilter);
			}
		}

		#if PRINT_FLAG_TAGS_ON_TIMER_INSTANCE
		{
			if(twle->twleType == TWLE_TIMER_INSTANCE){
				if(twle->timerInstance.flags.parentNotOpen){
					strcat(text, "(p)");
				}
				if(twle->timerInstance.flags.forcedOpen){
					strcat(text, "(o)");
				}
				if(twle->timerInstance.flags.forcedClosed){
					strcat(text, "(c)");
				}
				if(twle->timerInstance.flags.closedByDepth){
					strcat(text, "(d)");
				}
				strcatf(text, "%d/%d", twle->depthFromRoot, twl->maxDepth);
			}
		}
		#endif

		printWithShadow(md->dc,
						xOffset + 10,
						3,
						text,
						12,
						argb);
	}

	{
		char buffer[200];

		switch(twle->twleType){
			//xcase TWLE_GROUP:{
			//	printWithShadow(md->dc,
			//					5,
			//					2,
			//					twle->text,
			//					15,
			//					0xff4444bb);
			//}

			xcase TWLE_THREAD:{
				U64 cycles;
				
				sprintf(buffer,
						"%s",
						getCommaSeparatedU64(twle->thread.frameCount));

				printWithShadow(md->dc,
								400 + 120 * 0 - md->xIndent,
								2,
								buffer,
								15,
								0xffaaffaa);
				
				if(twl->flags.hasOSCycles){
					cycles = twle->thread.os.cyclesTotal;
				}else{
					switch(wd->shownData){
						xcase SHOWN_DATA_CPU_ACTIVE:
							cycles = twle->thread.cycles.active;
						xcase SHOWN_DATA_CPU_BLOCKING:
							cycles = twle->thread.cycles.blocking;
						xdefault:
							cycles = twle->thread.cycles.total;
					}
				}

				twleDrawColumns(md,
								cycles,
								0,
								twle->thread.frameCount,
								0,
								twle->thread.timerCount);

				twListDrawHistoryGraph(	msg,
										twle,
										&twle->thread.history,
										showEntireHistory,
										twle,
										&gp,
										twl->flags.hasOSCycles ?
											SHOWN_DATA_CPU_ACTIVE :
											wd->shownData,
										twl->flags.hasOSCycles ? 2 : 0);
			}
			
			xcase TWLE_SCAN_FRAMES:{
				twleDrawColumns(md,
								twle->twleThread->thread.scanCycles,
								twle->twleThread->thread.scanFrameCount,
								twle->twleThread->thread.scanFrameCount,
								0,
								0);

				twListDrawHistoryGraph(	msg,
										twle,
										&twle->twleThread->thread.scanHistory,
										showEntireHistory,
										twle->twleThread,
										&gp,
										SHOWN_DATA_CPU_ACTIVE,
										2);
			}
			
			xcase TWLE_OS_CYCLES:
			acase TWLE_OS_GROUP:
			acase TWLE_OS_TICKS_USER:
			acase TWLE_OS_TICKS_KERNEL:{
				U32 showOtherValue;
				U64 cyclesTotal;
				
				switch(twle->twleType){
					xcase TWLE_OS_TICKS_USER:
						showOtherValue = 3;
						cyclesTotal = twle->twleThread->thread.os.ticksTotal.user;
					xcase TWLE_OS_TICKS_KERNEL:
						showOtherValue = 4;
						cyclesTotal = twle->twleThread->thread.os.ticksTotal.kernel;
					xdefault:
						showOtherValue = 2;
						cyclesTotal = twle->twleThread->thread.os.cyclesTotal;
				}
				
				sprintf(buffer,
						"%s",
						getCommaSeparatedU64(twle->twleThread->thread.frameCount));

				printWithShadow(md->dc,
								400 + 120 * 0 - md->xIndent,
								2,
								buffer,
								15,
								0xffaaffaa);
				
				twleDrawColumns(md,
								cyclesTotal,
								twle->twleThread->thread.frameCount,
								twle->twleThread->thread.frameCount,
								0,
								0);

				twListDrawHistoryGraph(	msg,
										twle,
										&twle->twleThread->thread.history,
										showEntireHistory,
										twle->twleThread,
										&gp,
										SHOWN_DATA_CPU_ACTIVE,
										showOtherValue);
			}

			xcase TWLE_TIMER_ID:{
				TempWindowListEntry*	twleThread = twle->twleThread;
				S32						showCyclesOther = 0;
				U64						cycles;
				U32						hitCount;
				U32						frameCount;

				if(	!wd->flags.showCyclesOnly &&
					twle->timerID.timerType != PERFINFO_TYPE_CPU)
				{
					showCyclesOther = 1;
				}

				sprintf(buffer,
						"%d",
						twle->timerID.timerInstanceCount);

				printWithShadow(md->dc,
								10 + xOffset - 30,
								2,
								buffer,
								15,
								0xffdddd55);
				
				if(!hfFrame.frameIndex){
					hitCount = twle->timerID.count.hits;
					frameCount = twle->timerID.count.frames;

					if(showCyclesOther){
						cycles = twle->timerID.cycles.other;
					}else{
						switch(wd->shownData){
							xcase SHOWN_DATA_CPU_ACTIVE:
								cycles = twle->timerID.cycles.active;
							xcase SHOWN_DATA_CPU_BLOCKING:
								cycles = twle->timerID.cycles.blocking;
							xdefault:
								cycles = twle->timerID.cycles.total;
						}
					}
				}else{
					hitCount = hfCycles.count;
					frameCount = 0;

					if(showCyclesOther){
						cycles = hfCycles.cycles.other;
					}else{
						switch(wd->shownData){
							xcase SHOWN_DATA_CPU_ACTIVE:
								cycles = hfCycles.cycles.active;
							xcase SHOWN_DATA_CPU_BLOCKING:
								cycles = hfCycles.cycles.blocking;
							xdefault:
								cycles =	hfCycles.cycles.active +
											hfCycles.cycles.blocking;
						}
					}
				}

				sprintf(buffer,
						"%s",
						getCommaSeparatedU64(hitCount));

				printWithShadow(md->dc,
								400 + 120 * 0 - md->xIndent,
								2,
								buffer,
								15,
								0xffaaffaa);

				twleDrawColumns(md,
								cycles,
								hitCount,
								frameCount,
								0,
								0);

				twListDrawHistoryGraph(	msg,
										twle,
										&twle->timerID.history,
										showEntireHistory,
										twleThread,
										&gp,
										wd->shownData,
										showCyclesOther);
			}

			xcase TWLE_TIMER_INSTANCE:{
				TempWindowListEntry*	twleThread = twle->twleThread;
				TempWindowListEntry*	twleTimerID = twle->timerInstance.twleTimerID;
				S32						showCyclesOther = 0;
				U64						cycles;
				U32						hitCount;
				U32						frameCount;
				
				if(	!wd->flags.showCyclesOnly &&
					twleTimerID &&
					twleTimerID->timerID.timerType != PERFINFO_TYPE_CPU)
				{
					showCyclesOther = 1;
				}

				if(!hfFrame.frameIndex){
					hitCount = twle->timerInstance.count.hits;
					frameCount = twle->timerInstance.count.frames;

					if(showCyclesOther){
						cycles = twle->timerInstance.cycles.other;
					}else{
						switch(wd->shownData){
							xcase SHOWN_DATA_CPU_ACTIVE:
								cycles = twle->timerInstance.cycles.active;
							xcase SHOWN_DATA_CPU_BLOCKING:
								cycles = twle->timerInstance.cycles.blocking;
							xdefault:
								cycles = twle->timerInstance.cycles.total;
						}
					}
				}else{
					hitCount = hfCycles.count;
					frameCount = 0;

					if(showCyclesOther){
						cycles = hfCycles.cycles.other;
					}else{
						switch(wd->shownData){
							xcase SHOWN_DATA_CPU_ACTIVE:
								cycles = hfCycles.cycles.active;
							xcase SHOWN_DATA_CPU_BLOCKING:
								cycles = hfCycles.cycles.blocking;
							xdefault:
								cycles =	hfCycles.cycles.active +
											hfCycles.cycles.blocking;
						}
					}
				}

				sprintf(buffer,
						"%s",
						getCommaSeparatedU64(hitCount));

				printWithShadow(md->dc,
								400 + 120 * 0 - md->xIndent,
								2,
								buffer,
								15,
								0xffaaffaa);

				twleDrawColumns(md,
								cycles,
								hitCount,
								frameCount,
								twle->childDepth,
								twle->timerInstance.count.subTimers);

				twListDrawHistoryGraph(	msg,
										twleOriginal,
										&twle->timerInstance.history,
										showEntireHistory,
										twleThread,
										&gp,
										wd->shownData,
										showCyclesOther);
			}
		}

		#if 0
		if(	twle->twleType == TWLE_TIMER_INSTANCE &&
			twle->timerInstance.count.hits &&
			twle->parent &&
			twle->parent->twleType == TWLE_TIMER_INSTANCE &&
			twle->parent->timerInstance.count.hits &&
			twle->parent->timerInstance.cycles)
		{
			S32 percentOfParent =	100 *
									twle->timerInstance.cycles /
									twle->parent->timerInstance.cycles;

			suiDrawFilledRect(	md->dc,
								35,
								2,
								22,
								15,
								0xff222233);

			suiDrawFilledRect(	md->dc,
								36,
								3,
								percentOfParent * 20 / 100,
								13,
								0xff444499);
		}
		#endif
	}

	PERFINFO_AUTO_STOP();

	return 1;
}

static S32 twRefreshProcessListCallback(const ForEachProcessCallbackData* data){
	twConnectToLocalProcess(data->userPointer, data->pid, data->exeFileName, 0);
	
	return 1;
}

static void twRefreshProcessList(TempWindowWD* wd){
	forEachProcess(twRefreshProcessListCallback, wd);
}

static void lleStartTimerForNetClient(	LeftListEntry* lleProcess,
										LeftListEntry* lleClient,
										U32 clientStartID)
{
	TempWindowWD*			wd = lleProcess->wdOwner;
	LeftListEntryProcess*	p = &lleProcess->process;
	LeftListEntryNetClient*	c = &lleClient->netClient;
	LeftListEntry*			lleClientToProcess;
	char					cmdText[100];

	assert(lleProcess->lleType == LLE_PROCESS);
	assert(p->flags.isLocal);
	assert(lleClient->lleType == LLE_NET_CLIENT_PROFILER);
	
	lleCreate(	&lleClientToProcess,
				wd,
				lleClient,
				LLE_NET_CLIENT_TO_PROCESS,
				NULL,
				18);

	p->startID++;

	eaPush(	&p->local.lleNetClients,
			lleClientToProcess);

	lleClientToProcess->netClientToProcess.lleProcess = lleProcess;
	lleClientToProcess->netClientToProcess.lleClient = lleClient;
	lleClientToProcess->netClientToProcess.processStartID = p->startID;
	lleClientToProcess->netClientToProcess.clientStartID = clientStartID;

	sprintf(cmdText,
			"StartTimerWithID %u",
			p->startID);

	lleLocalProcessSendString(lleProcess, cmdText);
}

static void lleStopTimerForNetClient(	LeftListEntry* lleProcess,
										LeftListEntry* lleClient,
										LeftListEntry* lleClientToProcess)
{
	TempWindowWD*			wd = lleProcess->wdOwner;
	LeftListEntryProcess*	p = &lleProcess->process;
	LeftListEntryNetClient*	c = &lleClient->netClient;

	assert(lleProcess->lleType == LLE_PROCESS);
	assert(p->flags.isLocal);
	assert(lleClient->lleType == LLE_NET_CLIENT_PROFILER);

	EARRAY_CONST_FOREACH_BEGIN(p->local.lleNetClients, i, isize);
		LeftListEntry* lle = p->local.lleNetClients[i];
		
		assert(lle->lleType == LLE_NET_CLIENT_TO_PROCESS);

		if(	lle->netClientToProcess.processStartID &&
			lle->netClientToProcess.lleClient == lleClient &&
			(	!lleClientToProcess ||
				lleClientToProcess == lle))
		{
			char cmdText[100];

			sprintf(cmdText,
					"StopTimerWithID %u",
					lle->netClientToProcess.processStartID);

			lle->netClientToProcess.processStartID = 0;
			lle->netClientToProcess.clientStartID = 0;

			lleLocalProcessSendString(lleProcess, cmdText);

			lleDestroy(&lle);
			break;
		}
	EARRAY_FOREACH_END;
}

static void lleSendToHostStopTimer(LeftListEntry* lle){
	LeftListEntryProcess* p = &lle->process;

	assert(lle->lleType == LLE_PROCESS);

	if(TRUE_THEN_RESET(p->flags.timerStarted)){
		pfwDestroy(&p->pfw);

		if(p->flags.isLocal){
			char cmdText[100];

			sprintf(cmdText,
					"StopTimerWithID %u",
					p->local.startID);

			p->local.startID = 0;

			lleLocalProcessSendString(lle, cmdText);
		}
		else if(p->remote.lleNetClientProcess){
			LeftListEntry*	lleClient = p->remote.lleNetClientProcess;
			
			assert(lleClient->lleType == LLE_NET_CLIENT_PROCESS);
			
			if(lleClient->netClient.link){
				Packet* pak = pktCreate(lleClient->netClient.link, 0);

				pktSendString(pak, "StopTimer");
				pktSend(&pak);
			}
		}else{
			LeftListEntry*	lleHost = lle->parent;
			Packet*			pak;

			assert(lleHost->lleType == LLE_HOST);
			assert(!lleHost->host.flags.isLocal);

			if(lleHost->host.link){
				pak = pktCreate(lleHost->host.link, 0);

				pktSendString(pak, "StopTimer");
				pktSendBitsAuto(pak, lle->process.pid);
				pktSendBitsAuto(pak, lle->process.pidInstance);

				pktSend(&pak);
			}
		}

		lleDestroyTempWindowList(lle);
	}
}

static void lleSendToHostMyInfo(LeftListEntry* lleHost){
	Packet* pak;

	assert(lleHost->lleType == LLE_HOST);
	assert(!lleHost->host.flags.isLocal);

	pak = pktCreate(lleHost->host.link, 0);

	pktSendString(pak, "MyInfo");
	pktSendString(pak, getComputerName());
	pktSendString(pak, getUserName());

	pktSend(&pak);
}

static void lleSendToHostBigString(	LeftListEntry* lleHost,
									const char* bigString)
{
	Packet* pak;

	assert(lleHost->lleType == LLE_HOST);
	assert(!lleHost->host.flags.isLocal);

	pak = pktCreate(lleHost->host.link, 0);

	pktSendString(pak, "BigString");
	pktSendString(pak, bigString);

	pktSend(&pak);
}

static S32 lleHandleDraw(	SUIWindow* w,
							TempWindowWD* wd,
							const SUIWindowMsg* msg)
{
	const SUIListMsgEntryDraw*	md = msg->msgData;
	const SUIDrawContext*		dc = md->dc;
	const LeftListEntry*		lle = md->le.userPointer;
	char						buffer[1000] = "";

	PERFINFO_AUTO_START_FUNC();

	if(lle->lleType == LLE_NET_CLIENT_TO_PROCESS){
		lle = lle->netClientToProcess.lleProcess;
	}

	switch(lle->lleType){
		xcase LLE_TEST:{
			printWithShadow(dc, 5, 2, lle->text, md->sy - 4, 0xffeeeeff);
		}

		xcase LLE_HOST:{
			U32 argbText = 0xffffaa55;

			if(	lle->host.flags.isLocal ||
				lle->host.flags.isRemoteProcesses)
			{
				const char* prefix = lle->host.flags.isLocal ?
										"Local" :
										"Remote";

				if(!eaSize(&lle->children)){
					sprintf(buffer, "No %s Processes", prefix);
				}else{
					sprintf(buffer,
							"%d %s Process%s",
							eaSize(&lle->children),
							prefix,
							eaSize(&lle->children) > 1 ? "es" : "");
				}

				printWithShadow(dc, 5, 2, buffer, md->sy - 4, argbText);
			}else{
				S32 x = 25;
				S32 sx;
				
				if(linkConnected(lle->host.link)){
					F32 timeSincePacket = linkRecvTimeElapsed(lle->host.link);
					
					if(timeSincePacket >= 1.5f){
						strcatf(buffer, "%1.1fs - ", timeSincePacket);
					}
				}
				
				if(lle->host.computerName){
					strcatf(buffer, "%s", lle->host.computerName);
				}else{
					strcatf(buffer, "%s", lle->text);
				}
				
				suiWindowGetTextSize(w, buffer, md->sy - 4, &sx, NULL);
				printWithShadow(dc, x, 2, buffer, md->sy - 4, argbText);
				x += MAX(sx + 10, LEFT_LIST_DEFAULT_WIDTH - (S32)md->xIndent);

				// Print username/original address.
				
				buffer[0] = 0;
				
				if(lle->host.userName){
					sprintf(buffer, "%s", lle->host.userName);
				}

				if(	lle->host.computerName &&
					stricmp(lle->text, lle->host.computerName))
				{
					strcatf(buffer,
							"%s%s",
							buffer[0] ? " / " : "",
							lle->text);
				}
				
				printWithShadow(dc, x, 1, buffer, md->sy / 2, 0xff888888);

				{
					char		ipStr[100];
					const char* status = NULL;
					
					buffer[0] = 0;

					GetIpStr(lle->host.ip, SAFESTR(ipStr));

					if(	lle->host.ip &&
						stricmp(ipStr, lle->text))
					{
						sprintf(buffer, "%s", ipStr);
					}

					if(lle->host.hlt){
						status = "waiting for DNS";
					}
					else if(lle->host.link){
						if(!linkConnected(lle->host.link)){
							status = "waiting for link to connect";
						}
						else if(!lle->host.flags.hasReceivedData){
							status = "waiting for first data packet";
						}
					}
					
					if(status){
						strcatf(buffer, "%s%s", buffer[0] ? " / " : "", status);
					}
					
					if(buffer[0]){
						printWithShadow(dc, x, 1 + md->sy / 2, buffer, md->sy / 2, 0xff888888);
					}
				}

				//printWithShadow(dc, 25, 2, buffer, md->sy - 4, argbText);

				// Draw the connectivity indicator light.

				suiDrawRect(dc, 5, 5, md->sy - 10, md->sy - 10, 1, 0xff111122);
				suiDrawRect(dc, 6, 6, md->sy - 12, md->sy - 12, 1, 0xff000000);

				suiDrawFilledRect(	dc,
									7,
									7,
									md->sy - 14,
									md->sy - 14,
									lle->host.hlt ?
										0xffff00ff :
										lle->host.link ?
											linkConnected(lle->host.link) ?
												lle->host.flags.hasReceivedData ?
													0xff00ff00 :
													0xff00aa00 :
												0xffffff00 :
											0xffff0000);
			}
		}

		xcase LLE_NET_CLIENTS:{
			printWithShadow(dc, 25, 2, lle->text, md->sy - 4, 0xff6688bb);

			suiDrawRect(dc, 5, 5, md->sy - 10, md->sy - 10, 1, 0xff111122);
			suiDrawRect(dc, 6, 6, md->sy - 12, md->sy - 12, 1, 0xff000000);

			suiDrawFilledRect(	dc,
								7,
								7,
								md->sy - 14,
								md->sy - 14,
								wd->net.server.listen ?
									0xff00ff00 :
									0xffff0000);
		}

		xcase LLE_NET_CLIENT_PROFILER:{
			if(lle->netClient.userName){
				sprintf(buffer,
						"%s:%s (%s)",
						lle->netClient.computerName,
						lle->netClient.userName,
						lle->text);
			}else{
				strcpy(buffer, lle->text);
			}

			printWithShadow(dc, 5, 2, buffer, md->sy - 4, 0xffeeeeff);
		}

		xcase LLE_NET_CLIENT_PROCESS:{
			if(lle->netClient.userName){
				sprintf(buffer,
						"Process: %s:%s (%s)",
						lle->netClient.computerName,
						lle->netClient.userName,
						lle->text);
			}else{
				sprintf(buffer,
						"Process: %s",
						lle->text);
			}

			printWithShadow(dc, 5, 2, buffer, md->sy - 4, 0xffeeeeff);
		}

		xcase LLE_PROCESS:{
			const LeftListEntryProcess*	p = &lle->process;
			S32							x = 40;
			S32							sx;
			S32							textHeight = md->sy - 4;

			// Print the PID.

			sprintf(buffer,
					"%d",
					p->pid);

			if(p->pidInstance){
				strcatf(buffer, ".%d", p->pidInstance);
			}
			
			printWithShadow(dc,
							5,
							4,
							buffer,
							md->sy - 6,
							0xff888888);

			// Indicate currently viewed process.

			if(SAFE_MEMBER(p->twl, flags.isVisible)){
				const char* text = ">";
				U32			height = md->sy - 6;
				U32			tsx;
				U32			bsx = textHeight;
				
				suiWindowGetTextSize(w, text, height, &tsx, NULL);
				
				suiDrawFilledRect(	dc,
									md->sx - bsx - 2,
									2,
									bsx,
									textHeight,
									0xff446644);
									
				suiDrawRect(dc,
							md->sx - bsx - 2,
							2,
							bsx,
							textHeight,
							1,
							0xff559955);

				printWithShadow(dc,
								md->sx - bsx - 2 + ((bsx - tsx) / 2),
								2,
								text,
								height,
								0xff77ff77);
			}

			// Print the paused indicator.

			if(SAFE_MEMBER(p->twl, flags.ignoreUpdates)){
				const char* text = "PAUSED";
				S32			xSize;
				S32			ySize;

				suiWindowGetTextSize(w, text, textHeight, &xSize, &ySize);

				suiDrawFilledRect(dc, x - 2, 1, xSize + 4, md->sy - 3, 0xff666600);

				printWithShadow(dc,
								x,
								2,
								text,
								textHeight,
								0xffffff88);

				x += xSize + 4;
			}

			// Print the record indicator.

			if(p->pfw){
				const char* text = "REC";
				S32			xSize;
				S32			ySize;

				suiWindowGetTextSize(w, text, textHeight, &xSize, &ySize);

				suiDrawFilledRect(dc, x - 2, 1, xSize + 4, md->sy - 3, 0xff440000);

				printWithShadow(dc,
								x,
								2,
								text,
								textHeight,
								0xffff8888);

				x += xSize + 4;
			}

			// Print the process name.

			buffer[0] = 0;

			if(	!p->flags.isLocal &&
				p->remote.lleNetClientProcess)
			{
				strcatf(buffer,
						"%s: ",
						p->remote.lleNetClientProcess->netClient.computerName);
			}

			strcatf(buffer,
					"%s",
					getFileNameConst(p->exeName));

			if(strEndsWith(buffer, ".exe")){
				buffer[strlen(buffer) - 4] = 0;
			}

			if(	lle->process.flags.isLocal &&
				lle->process.pid == GetCurrentProcessId())
			{
				strcat(buffer, " (me)");
			}

			if(	p->updatesRemaining ||
				p->bytesRemaining)
			{
				strcatf(buffer,
						" (%dU, %dB)",
						p->updatesRemaining,
						p->bytesRemaining);
			}

			printWithShadow(dc,
							x,
							2,
							buffer,
							textHeight,
							!p->flags.receivedAnything ?
								0xffdd3333 :
								p->flags.timerStarted ?
									0xffaaffaa :
									p->flags.threadsAreActive ?
										0xffffffaa :
										0xff5555aa);

			suiWindowGetTextSize(w, buffer, textHeight, &sx, NULL);
			x += sx;
		
			buffer[0] = 0;
			
			if(p->osTimes.cycles){
				strcatf(buffer, "%s", getCommaSeparatedU64(p->osTimes.cycles));
			}
			else if(p->osTimes.ticksUser + p->osTimes.ticksKernel){
				F32 percent =	100.f*
								(F32)(p->osTimes.ticksUser + p->osTimes.ticksKernel) / 
								(F32)(10 * SQR(1000));

				strcatf(buffer,
						"%1.2f%%",
						percent);
			}

			if(buffer[0]){
				S32 xRight =	MAX(LEFT_LIST_DEFAULT_WIDTH, wd->leftList.width) -
								3 -
								(S32)md->xIndent -
								(SAFE_MEMBER(p->twl, flags.isVisible) ? 20 : 0);
								
				suiWindowGetTextSize(w, buffer, textHeight, &sx, NULL);
				
				MAX1(x, xRight - sx);
	
				printWithShadow(dc,
								x,
								2,
								buffer,
								textHeight,
								0xff444466);
			}
		}

		xcase LLE_GROUP:{
			printWithShadow(dc, 5, 2, lle->text, md->sy - 4, 0xff6688bb);
		}

		xcase LLE_COMMAND:{
			printWithShadow(dc, 5, 2, lle->text, md->sy - 4, 0xff66ff88);
		}

		xcase LLE_FILE:{
			printWithShadow(dc, 5, 2, lle->text, md->sy - 4, 0xffff8033);
		}

		xcase LLE_RECORDINGS_FOLDER:{
			U32 argb = lle->recordingFolder.flags.scanned ?
							0xffffff00 :
							lle->recordingFolder.flags.notFound ?
								0xffaa0000 :
								0xffaaaa00;
							
			printWithShadow(dc, 5, 2, lle->text, md->sy - 4, argb);
		}
	}

	PERFINFO_AUTO_STOP();

	return 1;
}

static void twDoTextEntryPopup(	TempWindowWD* wd,
								const char* promptText,
								TempWindowTextEntryType textEntryType,
								const char* text)
{
	wd->textEntryType = textEntryType;

	textEntryPopupSetPromptText(wd->wTextEntryPopup, promptText);

	textEntryPopupSetTarget(wd->wTextEntryPopup, 200, 1, text);
}

static void lleProcessRecordToggle(	LeftListEntry* lle,
									const char* fileNameOverride)
{
	LeftListEntryProcess*	p;
	LeftListEntry*			lleHost;
	TempWindowWD*			wd;
	
	if(!lle){
		return;
	}

	p = &lle->process;
	lleHost = lle->parent;
	wd = lle->wdOwner;

	assert(lle->lleType == LLE_PROCESS);
	assert(SAFE_MEMBER(lleHost, lleType) == LLE_HOST);

	if(!p->pfw){
		char fileName[MAX_PATH];
		
		lleSendToHostStartTimer(lle);

		if(fileNameOverride){
			strcpy(fileName, fileNameOverride);
		}else{
			char	dateStr[100];
			char*	dateChar;

			timeMakeLocalDateString(dateStr);

			for(dateChar = dateStr; *dateChar; dateChar++){
				if(	*dateChar < '0' ||
					*dateChar > '9')
				{
					*dateChar = '_';
				}
			}

			if(dirExists("C:/CrypticSettings")){
				sprintf(fileName,
						"C:/CrypticSettings/Profiler/%s",
						getFileNameConst(p->exeName));
			}else{
				sprintf(fileName,
						"%s",
						getFileNameConst(p->exeName));
			}

			if(strEndsWith(fileName, ".exe")){
				fileName[strlen(fileName) - 4] = 0;
			}

			if(lleHost->host.computerName){
				strcatf(fileName,
						".%s",
						lleHost->host.computerName);
			}

			strcatf(fileName,
					".%d.%s.pf",
					p->pid,
					dateStr);
		}

		mkdirtree(fileName);

		pfwCreate(&p->pfw, NULL, NULL);
		pfwStart(	p->pfw,
					fileName,
					0,
					SAFE_MEMBER(p->twl, atDecoder));
	}else{
		pfwDestroy(&p->pfw);
	}
	
	suiWindowInvalidate(wd->leftList.w);
}

static void twConfigWriteToDisk(TempWindowWD* wd);
static void lleRecordingFolderScan(LeftListEntry* lle);

static S32 lleHandleMouseDown(	SUIWindow* w,
								TempWindowWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIListMsgEntryMouse*		md = msg->msgData;
	SUIListEntry*					le = md->le.le;
	LeftListEntry*					lle = md->le.userPointer;
	TempWindowList*					twlVisible = NULL;
	
	if(!le){
		return 1;
	}
	
	twGetVisibleList(wd, &twlVisible);	

	if(md->button & SUI_MBUTTON_LEFT){
		S32 isOpen;

		switch(lle->lleType){
			xcase LLE_COMMAND:{
				if(lle == wd->leftList.cmd.lleCreateLocal){
					twListCreate(NULL, wd, NULL, 1, "Local", NULL);
				}
				else if(lle == wd->leftList.cmd.lleCreateThread){
					newTempWindowThread();
				}
				else if(lle == wd->leftList.cmd.lleCreateProcess){
					char cmd[] = "./Profiler.exe";
					system_detach(cmd, 0, 0);
				}
				else if(lle == wd->leftList.cmd.lleAddServer){
					twDoTextEntryPopup(	wd,
										"Enter server name:",
										TWTE_ADD_SERVER,
										NULL);
				}
				else if(lle == wd->leftList.cmd.lleSetFilter){
					twDoTextEntryPopup(	wd,
										"Enter timer name filter:",
										TWTE_SET_FILTER,
										SAFE_MEMBER(twlVisible, nameFilter));
				}
				else if(lle == wd->leftList.cmd.lleAddRecordingFolder){
					twDoTextEntryPopup(	wd,
										"Enter recording folder path:",
										TWTE_ADD_RECORDING_FOLDER,
										NULL);
				}
				else if(lle == wd->leftList.cmd.lleToggleRecord){
					lleProcessRecordToggle(SAFE_MEMBER(twlVisible, lleProcess), NULL);
				}
				else if(lle == wd->leftList.cmd.lleToggleBreakpoint){
					wd->flags.breakPointsEnabled = !wd->flags.breakPointsEnabled;
					
					lleSetText(	lle,
								wd->flags.breakPointsEnabled ?
									"Breakpoints: ON" :
									"Breakpoints: OFF");

					suiWindowInvalidate(wd->leftList.w);
				}
				else if(lle == wd->leftList.cmd.lleProfileNewProcesses){
					wd->flags.profileNewProcesses = !wd->flags.profileNewProcesses;

					lleSetText(	lle,
								wd->flags.profileNewProcesses ?
									"Profile New Processes: ON" :
									"Profile New Processes: OFF");

					suiWindowInvalidate(wd->leftList.w);
				}
				else if(lle == wd->leftList.cmd.lleConnectToAllServers){
					EARRAY_CONST_FOREACH_BEGIN(wd->leftList.lleNetServers, i, isize);
						LeftListEntry* lleHost = wd->leftList.lleNetServers[i];
						
						lleHostConnect(lleHost);
					EARRAY_FOREACH_END;
				}
				else if(lle == wd->leftList.cmd.lleDisconnectAllServers){
					EARRAY_CONST_FOREACH_BEGIN(wd->leftList.lleNetServers, i, isize);
						LeftListEntry* lleHost = wd->leftList.lleNetServers[i];
						
						lleHostDisconnect(lleHost);
					EARRAY_FOREACH_END;
				}
				else if(lle == wd->leftList.cmd.lleCyclePercentages){
					twleShowPercentages = !twleShowPercentages;
				}
			}
			xcase LLE_PROCESS:{
				if(lle->process.twl){
					twSetCurrentList(wd, lle->process.twl);
				}
			}
			xcase LLE_RECORDINGS_FOLDER:{
				if(suiListEntryGetOpenState(le, &isOpen)){
					suiListEntrySetOpenState(le, !isOpen);
				}
				
				if(!lle->recordingFolder.flags.scanned){
					suiListEntrySetOpenState(le, 1);
					lleRecordingFolderScan(lle);
				}
			}
			xdefault:{
				if(suiListEntryGetOpenState(le, &isOpen)){
					suiListEntrySetOpenState(le, !isOpen);
				}
			}
		}
	}
	else if(md->button & SUI_MBUTTON_RIGHT){
		switch(lle->lleType){
			xcase LLE_HOST:{
				if(lle == wd->leftList.lleHostLocal){
					twRefreshProcessList(wd);
				}
				else if(!lle->host.link &&
						!lle->host.hlt)
				{
					lleHostConnect(lle);
				}else{
					lleHostDisconnect(lle);
				}
			}

			xcase LLE_NET_CLIENT_PROFILER:
			acase LLE_NET_CLIENT_PROCESS:{
				lleDestroy(&lle);
			}

			xcase LLE_FILE:{
				char fullPath[MAX_PATH];

				sprintf(fullPath,
						"%s/%s",
						lle->parent->text,
						lle->file.fileNameRelative);

				twListCreate(NULL, wd, NULL, 0, lle->text, fullPath);
			}

			xcase LLE_PROCESS:{
				LeftListEntryProcess* p = &lle->process;

				if(!p->flags.timerStarted){
					lleSendToHostStartTimer(lle);
				}else{
					lleSendToHostStopTimer(lle);
				}

				suiWindowInvalidate(wd->leftList.w);
			}
			
			xcase LLE_RECORDINGS_FOLDER:{
				if(TRUE_THEN_RESET(lle->recordingFolder.flags.scanned)){
					suiListEntrySetOpenState(le, 0);

					fcnDestroy(&lle->recordingFolder.fcn);
						
					lleDestroyChildren(lle);

					suiWindowInvalidate(wd->leftList.w);
				}
			}
		}
	}
	else if(md->button & SUI_MBUTTON_MIDDLE){
		if(lle->lleType == LLE_HOST){
			if(lle == wd->leftList.lleHostLocal){
				twRefreshProcessList(wd);
			}else{
				lleDestroy(&lle);

				twConfigWriteToDisk(wd);
			}
		}
		else if(lle->lleType == LLE_PROCESS){
			lleProcessRecordToggle(lle, NULL);
		}
		else if(lle->lleType == LLE_RECORDINGS_FOLDER){
			if(eaFindAndRemove(&wd->config.cur.rfs, lle->recordingFolder.rf) < 0){
				assert(0);
			}

			StructDestroySafe(	parse_ProfilerConfigRecordingFolder,
								&lle->recordingFolder.rf);

			lleDestroy(&lle);

			twConfigWriteToDisk(wd);
		}
	}

	return 1;
}

static S32 lleHandleDestroyed(	SUIWindow* w,
								TempWindowWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIListMsgEntryDestroyed*	md = msg->msgData;
	LeftListEntry*					lle = md->le.userPointer;

	lleDestroy(&lle);

	return 1;
}

static S32 twGetLocalProcessByPID(	TempWindowWD* wd,
									U32 pid,
									U32 pidInstance,
									LeftListEntry** lleOut)
{
	EARRAY_CONST_FOREACH_BEGIN(wd->leftList.lleHostLocal->children, i, isize);
		LeftListEntry* lle = wd->leftList.lleHostLocal->children[i];

		if(	lle->process.pid == pid &&
			lle->process.pidInstance == pidInstance)
		{
			*lleOut = lle;
			return 1;
		}
	EARRAY_FOREACH_END;

	return 0;
}

static S32 netServerReceiveLeftListEntry(	TempWindowWD* wd,
											Packet* pak,
											LeftListEntry** lleOut)
{
	U32 pid = pktGetBitsAuto(pak);
	U32 pidInstance = pktGetBitsAuto(pak);

	return twGetLocalProcessByPID(wd, pid, pidInstance, lleOut);
}

static void netServerHandlePacket(	Packet* pak,
									S32 cmdUnused,
									NetLink* link,
									LeftListEntry* lleClient)
{
	TempWindowWD*	wd = linkGetListenUserData(link);
	char			cmdName[100] = "";
	LeftListEntry*	lleProcess;

	pktGetString(pak, SAFESTR(cmdName));
	
	switch(lleClient->lleType){
		xcase LLE_NET_CLIENT_PROFILER:{
			#define CMD_IS(x) !stricmp(cmdName, x)

			if(CMD_IS("MyInfo")){
				SAFE_FREE(lleClient->netClient.computerName);
				lleClient->netClient.computerName = pktMallocString(pak);

				SAFE_FREE(lleClient->netClient.userName);
				lleClient->netClient.userName = pktMallocString(pak);

				suiWindowInvalidate(wd->leftList.w);
			}
			else if(CMD_IS("StartTimer")){
				if(netServerReceiveLeftListEntry(wd, pak, &lleProcess)){
					U32 startID = pktGetBitsAuto(pak);

					lleStartTimerForNetClient(lleProcess, lleClient, startID);
				}
			}
			else if(CMD_IS("StopTimer")){
				if(netServerReceiveLeftListEntry(wd, pak, &lleProcess)){
					lleStopTimerForNetClient(lleProcess, lleClient, NULL);
				}
			}
			else if(CMD_IS("TimerSetBreakpoint") ||
					CMD_IS("TimerSetForcedOpen"))
			{
				if(netServerReceiveLeftListEntry(wd, pak, &lleProcess)){
					U32 threadID = pktGetBitsAuto(pak);
					U32 instanceID = pktGetBitsAuto(pak);
					S32 doEnableFlag = pktGetBits(pak, 1);

					lleSendToHostSetTimerFlag(	lleProcess,
												cmdName,
												threadID,
												instanceID,
												doEnableFlag);
				}
			}
			else if(CMD_IS("MyClientType")){
				if(!lleClient->netClient.flags.typeCannotChange){
					char typeName[100] = "";
					
					pktGetString(pak, SAFESTR(typeName));
					
					if(!stricmp(typeName, "Profiler")){
						// Ignore it.
					}
					else if(!stricmp(typeName, "Process")){
						LeftListEntry* lleRemoteProcess;
						
						lleSendToClientYourClientType(lleClient, typeName);
						
						lleClient->lleType = LLE_NET_CLIENT_PROCESS;
						
						SAFE_FREE(lleClient->netClient.computerName);
						lleClient->netClient.computerName = pktMallocString(pak);

						SAFE_FREE(lleClient->netClient.userName);
						lleClient->netClient.userName = pktMallocString(pak);
						
						suiWindowInvalidate(wd->leftList.w);
						
						assert(!lleClient->netClient.lleRemoteProcess);
							
						lleCreate(	&lleRemoteProcess,
									wd,
									wd->leftList.lleHostRemote,
									LLE_PROCESS,
									NULL,
									18);
									
						lleRemoteProcess->process.flags.receivedAnything = 1;
						lleRemoteProcess->process.exeName = pktMallocString(pak);
						lleRemoteProcess->process.pid = pktGetBitsAuto(pak);
									
						suiListEntrySetHiddenState(wd->leftList.lleHostRemote->le, 0);

						lleClient->netClient.lleRemoteProcess = lleRemoteProcess;

						lleRemoteProcess->process.remote.lleNetClientProcess = lleClient;
					}
				}
			}
			else if(CMD_IS("BigString")){
				char* s = pktMallocString(pak);
				
				printf("Client %p sent BigString:\n%s\n", lleClient, s);
				
				SAFE_FREE(s);
			}
			
			#undef CMD_IS
		}
		
		xcase LLE_NET_CLIENT_PROCESS:{
			LeftListEntryProcess* p = &lleClient->netClient.lleRemoteProcess->process;
			
			assert(!p->flags.isLocal);

			if(!stricmp(cmdName, "Buffer")){
				if(p->flags.gotStartID){
					FragmentedBuffer* fb;

					fbCreate(&fb, 1);

					while(1){
						U8	buffer[1000];
						U32 bytesCur = pktGetBitsAuto(pak);
						
						if(!bytesCur){
							break;
						}

						pktGetBytes(pak, bytesCur, buffer);

						fbWriteBuffer(fb, buffer, bytesCur);
					}

					if(p->twl){
						autoTimerDecoderDecode(p->twl->atDecoder, fb);
					}

					if(p->pfw){
						pfwWriteFragmentedBuffer(p->pfw, fb);
					}

					fbDestroy(&fb);
				}
			}
			else if(!stricmp(cmdName, "StartID")){
				U32 startID = pktGetBitsAuto(pak);
				
				if(p->startID == startID){
					p->flags.gotStartID = 1;
				}
			}
		}
	}
	
	// Can't change type after the first packet.
	
	lleClient->netClient.flags.typeCannotChange = 1;
}

static void netServerHandleConnect(	NetLink* link,
									LeftListEntry* lle)
{
	TempWindowWD*	wd = linkGetListenUserData(link);
	char			ipBuffer[100];

	assert(!lle);

	linkGetIpPortStr(link, SAFESTR(ipBuffer));

	lleCreate(	&lle,
				wd,
				wd->leftList.lleNetClients,
				LLE_NET_CLIENT_PROFILER,
				ipBuffer,
				18);

	linkSetUserData(link, lle);
	lle->netClient.link = link;

	assert(eaFind(&wd->net.server.lleClients, lle) < 0);
	eaPush(&wd->net.server.lleClients, lle);

	twUpdateNotifyIcon(wd);

	lleSendToClientMyInfo(lle);
	lleSendToClientProcessList(lle);
}

static void netServerHandleDisconnect(	NetLink* link,
										LeftListEntry* lle)
{
	if(lle){
		TempWindowWD* wd = linkGetListenUserData(link);

		//printf("Client %p disconnected.\n", lle);

		assert(	lle->lleType == LLE_NET_CLIENT_PROFILER ||
				lle->lleType == LLE_NET_CLIENT_PROCESS);

		if(lle->netClient.link == link){
			lle->netClient.link = NULL;
		}

		lleDestroy(&lle);
	}
}

static S32 lleGetProcessByPID(	LeftListEntry* lleHost,
								U32 pid,
								U32 pidInstance,
								LeftListEntry** lleOut)
{
	assert(lleHost->lleType == LLE_HOST);

	EARRAY_CONST_FOREACH_BEGIN(lleHost->children, i, isize);
		LeftListEntry* lle = lleHost->children[i];

		assert(lle->lleType == LLE_PROCESS);

		if(	lle->process.pid == pid &&
			lle->process.pidInstance == pidInstance)
		{
			*lleOut = lle;
			return 1;
		}
	EARRAY_FOREACH_END;

	return 0;
}

static S32 netClientReceiveLeftListEntry(	LeftListEntry* lleHost,
											Packet* pak,
											LeftListEntry** lleOut)
{
	U32 pid = pktGetBitsAuto(pak);
	U32 pidInstance = pktGetBitsAuto(pak);

	return lleGetProcessByPID(lleHost, pid, pidInstance, lleOut);
}

SA_RET_NN_STR static char* stringMakeLower(SA_PARAM_NN_STR char* s){
	char* sOrig = s;

	for(; *s; s++){
		*s = tolower(*s);
	}

	return sOrig;
}

static void netClientHandlePacket(	Packet* pak,
									S32 cmdUnused,
									NetLink* link,
									LeftListEntry* lleHost)
{
	TempWindowWD*	wd = lleHost->wdOwner;
	char			cmdName[100];
	LeftListEntry*	lle;

	assert(lleHost->lleType == LLE_HOST);
	assert(!lleHost->host.flags.isLocal);
	
	lleHost->host.flags.hasReceivedData = 1;

	#if 0
	{
		// Some crap to test if blocking activity is properly propagated past a bits timer.
		
		Packet* p = pktCreate(link, 0);
		
		PERFINFO_AUTO_START_BLOCKING("blocking", 1);
			START_BIT_COUNT(p, "testbits");
				PERFINFO_AUTO_START_BLOCKING("blocking", 1);
					Sleep(1);
					PERFINFO_AUTO_START("not blocking", 1);
						FOR_BEGIN(i, 1000);
							S32 x = i * i * i;
						FOR_END;
					PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
			STOP_BIT_COUNT(p);
		PERFINFO_AUTO_STOP();
	}
	#endif

	pktGetString(pak, SAFESTR(cmdName));

	if(!stricmp(cmdName, "Buffer")){
		if(netClientReceiveLeftListEntry(lleHost, pak, &lle)){
			U32 startID = pktGetBitsAuto(pak);

			if(lle->process.startID == startID){
				LeftListEntryProcess*	p = &lle->process;
				FragmentedBuffer*		fb;

				assert(!p->flags.isLocal);

				fbCreate(&fb, 1);

				while(pktGetBits(pak, 1)){
					U8	buffer[1000];
					U32 bytesCur = pktGetBitsAuto(pak);

					pktGetBytes(pak, bytesCur, buffer);

					fbWriteBuffer(fb, buffer, bytesCur);
				}

				if(p->twl){
					autoTimerDecoderDecode(p->twl->atDecoder, fb);
				}

				if(p->pfw){
					pfwWriteFragmentedBuffer(p->pfw, fb);
				}

				fbDestroy(&fb);
			}
		}
	}
	else if(!stricmp(cmdName, "ProcessTimes")){
		if(netClientReceiveLeftListEntry(lleHost, pak, &lle)){
			lle->process.osTimes.cycles = pktGetBits(pak, 1) ? pktGetBits64(pak, 64) : 0;
			lle->process.osTimes.ticksUser = pktGetBits(pak, 1) ? pktGetBits64(pak, 64) : 0;
			lle->process.osTimes.ticksKernel = pktGetBits(pak, 1) ? pktGetBits64(pak, 64) : 0;

			suiWindowInvalidate(wd->leftList.w);
		}
	}
	else if(!stricmp(cmdName, "YourClientType")){
		char clientTypeName[100] = "";
		
		pktGetString(pak, SAFESTR(clientTypeName));
		
		if(stricmp(clientTypeName, "Profiler")){
			assert(0);
		}
	}
	else if(!stricmp(cmdName, "MyInfo")){
		ProfilerConfigServer* pcs = lleHost->host.pcs;

		SAFE_FREE(lleHost->host.computerName);
		lleHost->host.computerName = stringMakeLower(pktMallocString(pak));

		SAFE_FREE(lleHost->host.userName);
		lleHost->host.userName = stringMakeLower(pktMallocString(pak));

		if(pcs){
			SAFE_FREE(pcs->computerName);
			pcs->computerName = StructAllocString(lleHost->host.computerName);

			SAFE_FREE(pcs->userName);
			pcs->userName = StructAllocString(lleHost->host.userName);

			twConfigWriteToDisk(wd);
		}

		suiWindowInvalidate(wd->leftList.w);
	}
	else if(!stricmp(cmdName, "ProcessAdd")){
		lleCreate(	&lle,
					lleHost->wdOwner,
					lleHost,
					LLE_PROCESS,
					NULL,
					18);

		lle->process.exeName = pktMallocString(pak);
		lle->process.pid = pktGetBitsAuto(pak);
		lle->process.pidInstance = pktGetBitsAuto(pak);
	}
	else if(!stricmp(cmdName, "ProcessRemove")){
		if(netClientReceiveLeftListEntry(lleHost, pak, &lle)){
			lleDestroy(&lle);
		}
	}
	else if(!stricmp(cmdName, "ThreadsAreActive")){
		if(netClientReceiveLeftListEntry(lleHost, pak, &lle)){
			S32 isActive = pktGetBits(pak, 1);

			lle->process.flags.threadsAreActive = !!isActive;
			suiWindowInvalidate(wd->leftList.w);
		}
	}
	else if(!stricmp(cmdName, "ReceivedAnything")){
		if(netClientReceiveLeftListEntry(lleHost, pak, &lle)){
			lle->process.flags.receivedAnything = 1;
			suiWindowInvalidate(wd->leftList.w);
		}
	}
}

static void netClientHandleConnect(	NetLink* link,
									LeftListEntry* lle)
{
	assert(lle->lleType == LLE_HOST);

	suiWindowInvalidate(lle->wdOwner->leftList.w);

	lleSendToHostMyInfo(lle);
}

static void netClientHandleDisconnect(	NetLink* link,
										LeftListEntry* lle)
{
	if(!lle){
		TempWindowWD* wd = linkGetListenUserData(link);

		if(wd){
			suiWindowInvalidate(wd->leftList.w);
		}

		return;
	}

	assert(lle->lleType == LLE_HOST);

	if(lle->host.link == link){
		linkRemove(&lle->host.link);
		
		lle->host.flags.hasReceivedData = 0;

		lleDestroyChildren(lle);
	}

	suiWindowInvalidate(lle->wdOwner->leftList.w);
}

typedef enum HostLookupThreadLockType {
	HLT_LOCK_BG_LOOKUP_COMPLETE = BIT(0),
	HLT_LOCK_BG_SENT_CALLBACK	= BIT(1),
	HLT_LOCK_FG_DESTROY			= BIT(2),
} HostLookupThreadLockType;

typedef struct HostLookupThread {
	void*						userPointer;
	HostLookupThreadCallback	callback;
	char*						hostName;
	U32							ip;
	U32							lock;
} HostLookupThread;

static U32 __stdcall hostLookupThreadMain(HostLookupThread* hlt){
	EXCEPTION_HANDLER_BEGIN
	
	autoTimerThreadFrameBegin(__FUNCTION__);
	
	hlt->ip = ipFromString(hlt->hostName);
	
	if(_InterlockedOr(&hlt->lock, HLT_LOCK_BG_LOOKUP_COMPLETE) == 0){
		PERFINFO_AUTO_START("callback", 1);
		hlt->callback(hlt, hlt->userPointer, hlt->hostName, hlt->ip);
		PERFINFO_AUTO_STOP();

		_InterlockedOr(&hlt->lock, HLT_LOCK_BG_SENT_CALLBACK);
	}else{
		// The owner canceled it before the callback, so we have to free it.
		
		PERFINFO_AUTO_START("cleanup", 1);
		SAFE_FREE(hlt->hostName);
		SAFE_FREE(hlt);
		PERFINFO_AUTO_STOP();
	}
	
	autoTimerThreadFrameEnd();
	
	EXCEPTION_HANDLER_END
	
	return 0;
}

static void hostLookupThreadCreate(	HostLookupThread** hltOut,
									HostLookupThreadCallback callback,
									void* userPointer,
									const char* hostName)
{
	HostLookupThread* hlt;

	if(	!hltOut ||
		!userPointer ||
		!callback)
	{
		return;
	}
	
	*hltOut = hlt = callocStruct(HostLookupThread);
	
	hlt->hostName = strdup(hostName);
	hlt->userPointer = userPointer;
	hlt->callback = callback;

	_beginthreadex(NULL, 0, hostLookupThreadMain, hlt, 0, NULL);
}

static void hostLookupThreadDestroy(HostLookupThread** hltInOut){
	HostLookupThread* hlt = SAFE_DEREF(hltInOut);
	
	if(!hlt){
		return;
	}

	if(_InterlockedOr(&hlt->lock, HLT_LOCK_FG_DESTROY) == 0){
		// Destroyed before callback, so we're done, let the thread clean up.
	}else{
		// Thread is sending a callback, wait until it is done.
		
		while(!(hlt->lock & HLT_LOCK_BG_SENT_CALLBACK)){
			Sleep(1);
		}

		SAFE_FREE(hlt->hostName);
		SAFE_FREE(hlt);
	}
	
	*hltInOut = NULL;
}

static S32 hostLookupThreadGetIP(	HostLookupThread* hlt,
									U32* ipOut)
{
	if(	ipOut &&
		hlt &&
		hlt->lock & 1)
	{
		*ipOut = hlt->ip;
		return !!*ipOut;
	}else{
		return 0;
	}
}

static void lleHostLookupCallback(	HostLookupThread* hlt,
									LeftListEntry* lle,
									const char* hostName,
									U32 ip)
{
	iocpAssociationTriggerCompletion(lle->host.iocaHostLookup);
}

static void lleHostClearHostLookupThread(LeftListEntry* lle){
	assert(lle->lleType == LLE_HOST);

	hostLookupThreadDestroy(&lle->host.hlt);
	iocpAssociationDestroy(lle->host.iocaHostLookup);
	while(lle->host.iocaHostLookup){
		iocpCheck(lle->wdOwner->iocp, INFINITE, 0, NULL);
	}
}

static void lleHostLookupIOCPMsgHandler(const IOCompletionMsg* msg){
	LeftListEntry* lle = msg->userPointer;
	
	switch(msg->msgType){
		xcase IOCP_MSG_IO_SUCCESS:{
			assert(!lle->host.link);

			if(hostLookupThreadGetIP(	lle->host.hlt,
										&lle->host.ip))
			{
				lle->host.link = commConnectIP(	lle->wdOwner->net.comm,
												LINKTYPE_TOUNTRUSTED_20MEG,
												LINK_COMPRESS |
													LINK_FORCE_FLUSH,
												lle->host.ip,
												lle->host.hostPort,
												netClientHandlePacket,
												netClientHandleConnect,
												netClientHandleDisconnect,
												0);

				if(lle->host.link){
					linkSetUserData(lle->host.link, lle);

					suiListEntrySetOpenState(lle->le, 1);

					lle->host.ip = linkGetIp(lle->host.link);
					
					if(lle->host.pcs){
						lle->host.pcs->ip = lle->host.ip;
						
						twConfigWriteToDisk(lle->wdOwner);
					}
				}
			}
			
			lleHostClearHostLookupThread(lle);

			suiWindowInvalidate(lle->wdOwner->leftList.w);
		}
		
		xcase IOCP_MSG_ASSOCIATION_DESTROYED:{
			lle->host.iocaHostLookup = NULL;
		}
		
		xdefault:{
			assert(0);
		}
	}
}

static void lleHostConnect(LeftListEntry* lle){
	if(	!lle->host.link &&
		!lle->host.hlt)
	{
		iocpAssociationCreate(	&lle->host.iocaHostLookup,
								lle->wdOwner->iocp,
								NULL,
								lleHostLookupIOCPMsgHandler,
								lle);

		hostLookupThreadCreate(	&lle->host.hlt,
								lleHostLookupCallback,
								lle,
								lle->host.hostName);

		suiWindowInvalidate(lle->wdOwner->leftList.w);

		lle->host.flags.hasReceivedData = 0;
	}
}

static void lleHostDisconnect(LeftListEntry* lle){
	assert(lle->lleType == LLE_HOST);

	if(!lle->host.flags.isLocal){
		lleHostClearHostLookupThread(lle);

		if(lle->host.link){

			linkSetUserData(lle->host.link, NULL);
			
			#if SEND_BIG_STRING_ON_DISCONNECT
			{
				S32		size = 1024 * 1024;
				char*	s = malloc(size);
				
				FOR_BEGIN(i, size);
					char c = rand() % 27;
					
					if(c < 26){
						s[i] = 'a' + c;
					}else{
						s[i] = '\n';
					}
				FOR_END;
				
				s[size - 1] = 0;
				lleSendToHostBigString(lle, s);
				SAFE_FREE(s);
			}
			#endif

			linkFlushAndClose(&lle->host.link, __FUNCTION__);
		}

		lleDestroyChildren(lle);

		suiWindowInvalidate(lle->wdOwner->w);
	}
}

static void lleFileSetText(LeftListEntry* lle){
	char name[MAX_PATH];
	char fullPath[MAX_PATH];

	if(!lle){
		return;
	}

	assert(lle->lleType == LLE_FILE);

	strcpy(name, lle->file.fileNameRelative);

	if(strEndsWith(name, ".pf")){
		name[strlen(name) - 3] = 0;
	}
	else if(strEndsWith(name, ".profiler")){
		name[strlen(name) - 9] = 0;
	}

	sprintf(fullPath, "%s/%s", lle->parent->text, lle->file.fileNameRelative);

	strcatf(name, " (%s bytes)", getCommaSeparatedU64(fileSize(fullPath)));

	SAFE_FREE(lle->text);
	lle->text = strdup(name);
}

static void lleFolderAddFile(	LeftListEntry* lleFolder,
								const char* fileNameRelative)
{
	LeftListEntry* lle;

	if(	!strEndsWith(fileNameRelative, ".pf") &&
		!strEndsWith(fileNameRelative, ".profiler"))
	{
		return;
	}

	lleCreate(	&lle,
				lleFolder->wdOwner,
				lleFolder,
				LLE_FILE,
				NULL,
				18);

	lle->file.fileNameRelative = strdup(fileNameRelative);

	lleFileSetText(lle);
}

static FileScanAction addLocalFilesCallback(const char* dir,
											const struct _finddata32_t* fd,
											LeftListEntry* lleFolder)
{
	if(fd->attrib & _A_SUBDIR){
		return FSA_NO_EXPLORE_DIRECTORY;
	}

	lleFolderAddFile(lleFolder, fd->name);

	return FSA_NO_EXPLORE_DIRECTORY;
}

static void twNetServerListen(TempWindowWD* wd){
	if(!wd->net.server.listen){
		wd->net.server.listen = commListen(	wd->net.comm,
											LINKTYPE_TOUNTRUSTED_20MEG,
											//LINK_NO_COMPRESS |
												LINK_FORCE_FLUSH,
											CRYPTIC_PROFILER_PORT,
											netServerHandlePacket,
											netServerHandleConnect,
											netServerHandleDisconnect,
											0);

		if(wd->net.server.listen){
			listenSetUserData(wd->net.server.listen, wd);
			suiWindowInvalidate(wd->leftList.w);
		}
	}
}

static void twAddServer(TempWindowWD* wd,
						ProfilerConfigServer* pcs,
						const char* hostNameParam,
						S32 doConnect,
						U32 ip);

static void twAddRecordingFolder(	TempWindowWD* wd,
									const char* folderPath,
									ProfilerConfigRecordingFolder* rf);

static void twConfigReadFromDisk(TempWindowWD* wd){
	if(	!fileExists(configFileNameCurrent) &&
		fileExists(configFileNameNext))
	{
		S32 hadError = rename(	configFileNameNext,
								configFileNameCurrent);
	}
	
	ParserReadTextFile(	configFileNameCurrent,
						parse_ProfilerConfig,
						&wd->config.saved,
						0);

	StructCopyFields(	parse_ProfilerConfig,
						&wd->config.saved,
						&wd->config.cur,
						0, 0);

	EARRAY_CONST_FOREACH_BEGIN(wd->config.cur.servers, i, isize);
		ProfilerConfigServer* pcs = wd->config.cur.servers[i];

		forwardSlashes(pcs->hostName);
		twAddServer(wd, pcs, pcs->hostName, 0, pcs->ip);
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(wd->config.cur.rfs, i, isize);
		ProfilerConfigRecordingFolder* rf = wd->config.cur.rfs[i];

		if(!SAFE_DEREF(rf->path)){
			continue;
		}

		twAddRecordingFolder(wd, rf->path, rf);
	EARRAY_FOREACH_END;
}

static void twConfigWriteToDisk(TempWindowWD* wd){
	PERFINFO_AUTO_START_FUNC();

	if(StructCompare(	parse_ProfilerConfig,
						&wd->config.cur,
						&wd->config.saved,
						0, 0, 0))
	{
		S32 hadError = 0;

		StructCopyFields(	parse_ProfilerConfig,
							&wd->config.cur,
							&wd->config.saved,
							0, 0);

		unlink(configFileNameOldNext);

		if(fileExists(configFileNameNext)){
			hadError |= rename(	configFileNameNext,
								configFileNameOldNext);
		}

		ParserWriteTextFile(configFileNameNext,
							parse_ProfilerConfig,
							&wd->config.saved,
							0, 0);
		
		if(fileExists(configFileNameCurrent)){
			unlink(configFileNamePrevious);

			hadError |= rename(	configFileNameCurrent,
								configFileNamePrevious);
		}

		hadError |= rename(	configFileNameNext,
							configFileNameCurrent);
	}

	PERFINFO_AUTO_STOP();
}

static void twIOCPDoneWaitingInThread(	IOCompletionPort* iocp,
										TempWindowWD* wd)
{
	ASSERT_TRUE_AND_RESET(wd->flags.iocpWaitingInThread);
}

static void twIOCPEventCallback(SUIMainWindow* mw,
								TempWindowWD* wd,
								void* hEvent)
{
	S32 moreCompletionsAvailable = 0;
	
	PERFINFO_AUTO_START_FUNC();
	
	ASSERT_TRUE_AND_RESET(wd->flags.iocpWaitingInThread);
	
	iocpThreadWaitFinished(wd->iocp);
	
	if(	iocpCheck(wd->iocp, 0, 1, &moreCompletionsAvailable) &&
		moreCompletionsAvailable)
	{
		suiWindowProcessingHandleCreate(wd->w, &wd->phIOCompletionsAvailable);
	}
	else if(iocpCheckInThread(wd->iocp, twIOCPDoneWaitingInThread, wd)){
		wd->flags.iocpWaitingInThread = 1;
	}
	
	PERFINFO_AUTO_STOP();
}

static S32 twHandleCreate(	SUIWindow* w,
							TempWindowWD* wd,
							const SUIWindowMsg* msg)
{
	const TempWindowCreateParams* cp = msg->msgData;

	// Create wd and store pointers.

	assert(!wd);
	wd = callocStruct(TempWindowWD);

	wd->mw = cp->mw;
	wd->w = w;
	wd->leftList.width = 0;
	wd->leftList.widthTarget = LEFT_LIST_DEFAULT_WIDTH;
	wd->leftList.timeStart = 0;
	suiWindowProcessingHandleCreate(w, &wd->leftList.ph);

	suiWindowSetUserPointer(w, twMsgHandler, wd);

	// Init the notification icon.

	twUpdateNotifyIcon(wd);

	// Create buttons.

	suiButtonCreate(&wd->wButtonLeftListToggle, w, NULL, w);

	suiButtonCreate(&wd->wButtonYScaleInc, w, "Y+", w);
	suiButtonCreate(&wd->wButtonYScaleDec, w, "Y-", w);

	suiButtonCreate(&wd->wButtonXScaleInc, w, "X+", w);
	suiButtonCreate(&wd->wButtonXScaleDec, w, "X-", w);

	//suiButtonCreate(&wd->wButtonBreakPointToggle, w, "Break:OFF", w);
	suiButtonCreate(&wd->wButtonPauseToggle, w, "Pause", w);
	suiButtonCreate(&wd->wButtonClear, w, "Clear", w);

	suiButtonCreate(&wd->wButtonShownData, w, NULL, w);
	suiButtonSetSendDrawMsg(wd->wButtonShownData, 1);
	
	suiButtonCreate(&wd->wButtonIndentInc, w, ">", w);
	suiButtonCreate(&wd->wButtonIndentDec, w, "<", w);

	//suiButtonCreate(&wd->wButtonOptions, w, "Options", w);
	
	suiButtonCreate(&wd->wButtonCyclesOnlyToggle, w, NULL, w);
	suiButtonSetSendDrawMsg(wd->wButtonCyclesOnlyToggle, 1);

	// Create the left-side list.

	if(suiListCreateBasic(&wd->leftList.w, w, wd, w)){
		// Create the list entry class, which controls who gets the pipe msgs.

		suiListEntryClassCreate(&wd->leftList.lec,
								wd->leftList.w,
								w);

		// Create the command group.

		lleCreate(	&wd->leftList.lleCommands,
					wd,
					NULL,
					LLE_GROUP,
					"Commands",
					0);

		// Create the commands.

		#define CMD_CREATE(lle, name)				\
			lleCreate(	&wd->leftList.cmd.lle,		\
						wd,							\
						wd->leftList.lleCommands,	\
						LLE_COMMAND,				\
						name,						\
						18)

		CMD_CREATE(lleCreateLocal, "New Direct Profile");
		CMD_CREATE(lleCreateThread, "New Thread");
		CMD_CREATE(lleCreateProcess, "New Process");
		CMD_CREATE(lleAddServer, "Add Server (A)");
		CMD_CREATE(lleSetFilter, "Set Name Filter (F)");
		CMD_CREATE(lleAddRecordingFolder, "Add Recording Folder");
		CMD_CREATE(lleToggleRecord, "Toggle Record (Ctrl-R)");
		CMD_CREATE(lleToggleBreakpoint, "Breakpoints: OFF");
		CMD_CREATE(lleProfileNewProcesses, "Profile New Processes: OFF");
		CMD_CREATE(lleConnectToAllServers, "Connect to All Servers");
		CMD_CREATE(lleDisconnectAllServers, "Disconnect All Servers");
		CMD_CREATE(lleCyclePercentages, "Toggle relative percentage display");

		#undef CMD_CREATE

		// Create the recordings group.

		lleCreate(	&wd->leftList.lleRecordings,
					wd,
					NULL,
					LLE_GROUP,
					"Recordings",
					0);

		// Create the net clients group.

		lleCreate(	&wd->leftList.lleNetClients,
					wd,
					NULL,
					LLE_NET_CLIENTS,
					"Net Clients",
					0);

		suiListEntrySetOpenState(wd->leftList.lleNetClients->le, 1);

		// Create the local host group.

		lleCreate(	&wd->leftList.lleHostLocal,
					wd,
					NULL,
					LLE_HOST,
					NULL,
					0);

		wd->leftList.lleHostLocal->host.flags.isLocal = 1;
		suiListEntrySetOpenState(wd->leftList.lleHostLocal->le, 1);

		// Create the local host group.

		lleCreate(	&wd->leftList.lleHostRemote,
					wd,
					NULL,
					LLE_HOST,
					"Remote Processes",
					0);
		
		wd->leftList.lleHostRemote->host.flags.isRemoteProcesses = 1;
		suiListEntrySetOpenState(wd->leftList.lleHostRemote->le, 1);
		suiListEntrySetHiddenState(wd->leftList.lleHostRemote->le, 1);
	}

	// Create the io completion port shared by everything.

	iocpCreate(&wd->iocp);
	
	// Create the pipe server to wait for ConnectOnStartup requests.

	psCreate(	&wd->psConnectOnStartup,
				STACK_SPRINTF(	"CrypticProfilerConnectOnStartup%d",
								GetCurrentProcessId()),
				wd->iocp,
				wd,
				twPipeServerConnectAtStartupMsgHandler);

	// Search for local processes.

	twRefreshProcessList(wd);

	// Create the network stuff.

	suiWindowProcessingHandleCreate(w, &wd->phTempForNetComm);

	wd->net.comm = commCreate(0, 1);
	commEnableForcedSendThreadFrames(wd->net.comm, 1);

	twNetServerListen(wd);

	// Create main view container.

	suiWindowCreate(&wd->wViewContainer, w, containerMsgHandler, NULL);

	// Create the text entry popup.

	{
		TextEntryPopupCreateParams cpNew = {0};

		//cpNew.wdOwner = wd;
		cpNew.wReader = w;

		suiWindowCreate(&wd->wTextEntryPopup,
						w,
						textEntryPopupMsgHandler,
						&cpNew);

		suiWindowSetPosAndSize(wd->wTextEntryPopup, 350, -100, 500, 100);
	}

	// Load the config file.

	twConfigReadFromDisk(wd);

	if(!eaSize(&wd->config.cur.rfs)){
		twAddRecordingFolder(wd, "c:/CrypticSettings/profiler", NULL);
	}

	twConfigWriteToDisk(wd);

	// Done.

	// Check for command-line option to load/play a specific profile recording.

	if(cmdLine.autoLoadProfile[0]){
		twListCreate(NULL, wd, NULL, 0, cmdLine.autoLoadProfile, cmdLine.autoLoadProfile);
		cmdLine.autoLoadProfile[0] = '\0';
	}
	
	// Check for command-line option to record a PID and exit.
	
	if(	cmdLine.record.pid &&
		cmdLine.record.seconds)
	{
		S32 found = 0;

		EARRAY_CONST_FOREACH_BEGIN(wd->leftList.lleHostLocal->children, i, isize);
		{
			LeftListEntry* lle = wd->leftList.lleHostLocal->children[i];
			
			assert(lle->lleType == LLE_PROCESS);
			
			if(lle->process.pid == cmdLine.record.pid){
				found = 1;
				lleProcessRecordToggle(lle, cmdLine.record.fileName);
				autoTimerDecoderDestroy(&lle->process.twl->atDecoder);
				wd->recordAndExit.msStartTime = timeGetTime();
				wd->recordAndExit.exeName = strdup(lle->process.exeName);
				suiWindowProcessingHandleCreate(w, &wd->recordAndExit.ph);
				suiMainWindowMinimize(wd->mw);
				break;
			}
		}
		EARRAY_FOREACH_END;
		
		cmdLine.record.pid = 0;
		
		if(!found){
			suiMainWindowDestroy(&wd->mw);
			ExitProcess(1);
		}
	}

	twReflow(wd);
	
	{
		HANDLE eventHandle;

		if(iocpGetThreadWaitFinishedEvent(wd->iocp, &eventHandle)){
			suiMainWindowAddEvent(wd->mw, eventHandle, wd, twIOCPEventCallback);
		}
	}

	//suiWindowProcessingHandleCreate(w, &wd->ph);

	return 1;
}

static S32 twHandleDestroy(	SUIWindow* w,
							TempWindowWD* wd,
							const SUIWindowMsg* msg)
{
	while(wd->twls){
		TempWindowList* twl = wd->twls[0];

		twListDestroy(wd, &twl);
	}

	lleDestroy(&wd->leftList.lleNetClients);
	suiWindowDestroy(&wd->leftList.w);
	assert(!wd->leftList.lleNetClients);
	assert(!wd->leftList.lleNetServers);
	assert(!wd->leftList.lleRecordings);
	assert(!wd->leftList.lleHostLocal);
	assert(!wd->leftList.lleHostRemote);
	assert(!wd->leftList.lleCommands);

	suiWindowDestroy(&wd->wButtonLeftListToggle);
	suiWindowDestroy(&wd->wButtonYScaleInc);
	suiWindowDestroy(&wd->wButtonYScaleDec);

	suiWindowDestroy(&wd->wButtonXScaleInc);
	suiWindowDestroy(&wd->wButtonXScaleDec);

	suiWindowDestroy(&wd->wButtonBreakPointToggle);
	suiWindowDestroy(&wd->wButtonUseFrameToggle);
	suiWindowDestroy(&wd->wButtonPauseToggle);
	suiWindowDestroy(&wd->wButtonClear);
	suiWindowDestroy(&wd->wButtonShownData);
	suiWindowDestroy(&wd->wButtonOptions);
	suiWindowDestroy(&wd->wButtonCyclesOnlyToggle);

	suiWindowDestroy(&wd->wButtonIndentInc);
	suiWindowDestroy(&wd->wButtonIndentDec);
	
	psDestroy(&wd->psConnectOnStartup);
	iocpDestroy(&wd->iocp);
	
	fbReaderDestroy(&wd->fbr);
	
	SAFE_FREE(wd);
	
	return 1;
}

static S32 twHandleProcess(	SUIWindow* w,
							TempWindowWD* wd,
							const SUIWindowMsg* msg)
{
	PERFINFO_AUTO_START_FUNC();

	twNetServerListen(wd);

	commMonitor(wd->net.comm);
	
	if(wd->recordAndExit.ph){
		U32		msExpired = timeGetTime() - wd->recordAndExit.msStartTime;
		char	name[1000];

		if(msExpired / 1000 >= cmdLine.record.seconds){
			EARRAY_CONST_FOREACH_BEGIN(wd->leftList.lleHostLocal->children, i, isize);
			{
				lleSendToHostStopTimer(wd->leftList.lleHostLocal->children[i]);
			}
			EARRAY_FOREACH_END;

			suiMainWindowDestroy(&wd->mw);
			ExitProcess(0);
		}

		sprintf(name,
				"Profiler: Recording PID %u (%s) for %u seconds (%1.2f remaining) to file \"%s\"",
				cmdLine.record.pid,
				wd->recordAndExit.exeName,
				cmdLine.record.seconds,
				(F32)cmdLine.record.seconds - (F32)msExpired / 1000.f,
				cmdLine.record.fileName);

		suiMainWindowSetName(wd->mw, name);
	}

	if(!wd->flags.iocpWaitingInThread){
		iocpCheck(wd->iocp, 0, 10, NULL);

		if(iocpCheckInThread(wd->iocp, twIOCPDoneWaitingInThread, wd)){
			wd->flags.iocpWaitingInThread = 1;
			suiWindowProcessingHandleDestroy(w, &wd->phIOCompletionsAvailable);
		}
	}

	if(wd->leftList.width != wd->leftList.widthTarget){
		U32 		curTime = timeGetTime();
		S32 		diff = wd->leftList.widthTarget - wd->leftList.width;
		U32 		diffTime;
		S32 		diffStart;
		F32 		scale;
		const U32	duration = 300;

		if(!wd->leftList.timeStart){
			wd->leftList.timeStart = curTime;
			wd->leftList.widthStart = wd->leftList.width;
		}
		
		diffTime = curTime - wd->leftList.timeStart;
		MIN1(diffTime, duration);

		scale = SQR((F32)(duration - diffTime) / (F32)duration);
		
		diffStart = wd->leftList.widthStart - wd->leftList.widthTarget;
		
		wd->leftList.width = wd->leftList.widthTarget + scale * diffStart;
		
		twReflow(wd);

		if(wd->leftList.width == wd->leftList.widthTarget){
			wd->leftList.timeStart = 0;
			suiWindowProcessingHandleDestroy(w, &wd->leftList.ph);
		}
	}

	EARRAY_CONST_FOREACH_BEGIN(wd->twls, i, isize);
		TempWindowList* twl = wd->twls[i];

		if(twl->pfr){
			U32 startTime = timeGetTime();

			PERFINFO_AUTO_START("read file", 1);

			while(1){
				FragmentedBuffer* fb;
				
				if(!pfrReadFragmentedBuffer(twl->pfr, &fb)){
					break;
				}

				autoTimerDecoderDecode(twl->atDecoder, fb);
				fbDestroy(&fb);

				if(timeGetTime() - startTime >= 10){
					break;
				}
			}

			PERFINFO_AUTO_STOP();
		}
		else if(twl->atReader){
			PERFINFO_AUTO_START("read local", 1);

			twl->msStartReading = timeGetTime();

			autoTimerReaderRead(twl->atReader);

			PERFINFO_AUTO_STOP();
		}

		if(	twl->redrawInterval &&
			(	!twl->timeWhenInvalidated ||
				timeGetTime() - twl->timeWhenInvalidated >= twl->redrawInterval))
		{
			twl->redrawInterval = 0;
			twl->timeWhenInvalidated = timeGetTime();

			suiWindowInvalidate(twl->wFrame);
			suiWindowInvalidate(twl->wList);
		}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();

	return 1;
}

static S32 twHandleDraw(SUIWindow* w,
						TempWindowWD* wd,
						const SUIWindowMsg* msg)
{
	PERFINFO_AUTO_START("tempWindowDraw", 1);
	{
		const SUIDrawContext*	dc = msg->msgData;
		char					buffer[100];
		S32						sx;
		S32						sy;

		suiWindowGetSize(w, &sx, &sy);

		if(0){
			sprintf(buffer,
					"Thread %d",
					GetCurrentThreadId());

			suiPrintText(	dc,
							10,
							10,
							buffer,
							-1,
							15,
							0xff888888);
		}

		// Draw rect around process list.

		suiDrawRect(dc,
					10 - 2,
					10 - 2,
					wd->leftList.width + 2 * 2,
					sy - 10 - 10 + 2 * 2,
					1,
					0xff333333);

		suiDrawRect(dc,
					10 - 1,
					10 - 1,
					wd->leftList.width + 2,
					sy - 10 - 10 + 2,
					1,
					0xff555555);

		// Draw dragging indicator.

		if(	wd->flags.dragging ||
			wd->flags.mouseOverSplitter)
		{
			suiDrawRect(dc,
						10 + wd->leftList.width + 3,
						10 - 2 + suiWindowGetSizeY(wd->wButtonLeftListToggle),
						4,
						sy - 10 - 10 + 2 * 2 - suiWindowGetSizeY(wd->wButtonLeftListToggle),
						1,
						wd->flags.dragging ? 0xff7777ff : 0xff444499);
		}

		// Draw rect around un-framed TempWindowLists.

		if(eaSize(&wd->twls)){
			suiDrawRect(dc,
						10 + wd->leftList.width + 10 - 2,
						40 - 2,
						(sx - 10) - (10 + wd->leftList.width + 10) + 2 * 2,
						sy - 10 - 40 + 2 * 2,
						2,
						0xff333333);

			suiDrawRect(dc,
						10 + wd->leftList.width + 10 - 1,
						40 - 1,
						(sx - 10) - (10 + wd->leftList.width + 10) + 2,
						sy - 10 - 40 + 2,
						1,
						0xff555555);
		}
	}
	PERFINFO_AUTO_STOP();

	return 1;
}

static S32 twHandleMouseDown(	SUIWindow* w,
								TempWindowWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseButton* md = msg->msgData;

	if(	md->button & SUI_MBUTTON_LEFT &&
		!wd->flags.dragging &&
		md->x >= 10 + wd->leftList.width &&
		md->x < 10 + wd->leftList.width + 10)
	{
		if(msg->msgType == SUI_WM_MOUSE_DOUBLECLICK){
			if(wd->leftList.widthTarget){
				wd->leftList.widthTarget = 0;
			}else{
				wd->leftList.widthTarget = LEFT_LIST_DEFAULT_WIDTH;
			}

			if(	!wd->leftList.ph &&
				wd->leftList.width != wd->leftList.widthTarget)
			{
				wd->leftList.timeStart = 0;
				suiWindowProcessingHandleCreate(w, &wd->leftList.ph);
			}
		}else{
			wd->flags.dragging = 1;
			wd->dragSplitterOffsetX = md->x - (10 + wd->leftList.width);

			suiWindowInvalidate(w);
		}

		return 1;
	}

	return 0;
}

static S32 twHandleMouseMove(	SUIWindow* w,
								TempWindowWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseMove* md = msg->msgData;

	if(wd->flags.dragging){
		wd->leftList.width = md->x - 10 - wd->dragSplitterOffsetX;
		MINMAX1(wd->leftList.width, 0, 1000);
		wd->leftList.widthTarget = wd->leftList.width;
		wd->leftList.timeStart = 0;
		twReflow(wd);

		return 1;
	}
	else if(md->x >= 10 + wd->leftList.width &&
			md->x < 10 + wd->leftList.width + 10)
	{
		if(FALSE_THEN_SET(wd->flags.mouseOverSplitter)){
			suiWindowInvalidate(w);
		}

		return 1;
	}
	else if(TRUE_THEN_RESET(wd->flags.mouseOverSplitter)){
		suiWindowInvalidate(w);
	}

	return 0;
}

static void twleSetOpenState(	TempWindowListEntry* twle,
								S32 isOpen)
{
	if(!twle){
		return;
	}
	
	twle->flags.isOpen = !!isOpen;
	
	suiListEntrySetOpenState(twle->le, isOpen);
}

static void twleScrollToMe(	TempWindowListEntry* twle,
							TempWindowList* twl)
{
	TempWindowListEntry*	twleParent;
	TempWindowListEntry*	twleThread;
	S32						yPos;
	S32						height;

	if(!twle){
		return;
	}

	if(twleGetThread(twle, &twleThread)){
		twleThread->thread.flags.hasVisibleTimerIDs = 1;
	}

	for(twleParent = twle->parent;
		twleParent;
		twleParent = twleParent->parent)
	{
		twleSetHiddenState(twleParent, 0);
		twleSetOpenState(twleParent, 1);
	}

	if(	suiListEntryGetPosY(twle->le, &yPos) &&
		suiListEntryGetHeight(twle->le, &height))
	{
		suiListSetPosY(twl->wList, yPos + height / 2 - suiWindowGetSizeY(twl->wList) / 2);
	}
}

static S32 twleHandleMouseDown(	SUIWindow* w,
								TempWindowWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIListMsgEntryMouse*	md = msg->msgData;
	TempWindowList*				twl = msg->pipe.userPointer;
	TempWindowListEntry*		twle = md->le.userPointer;
	TempWindowListEntry*		twleOriginal = twle;
	SUIListEntry*				le = md->le.le;
	S32							x = md->x + md->xIndent;
	S32							y = md->y;
	GraphPosition				gp;
	S32							hasGraphPos;

	if(	!le ||
		twl->dragAnchorCycles)
	{
		return 1;
	}

	hasGraphPos = twleGetGraphPos(twle, md->sx, md->xIndent, &gp);

	if(md->button & SUI_MBUTTON_LEFT){
		S32 toggleOpen = 1;

		if(hasGraphPos){
			if(	x >= gp.x &&
				x < gp.x + gp.sx &&
				y >= gp.y &&
				y <= gp.y + gp.sy)
			{
				toggleOpen = 0;

				assert(gp.sx);

				if(twle->flags.showEntireHistory){
					twl->cyclesMaxLocked =	twl->cyclesMinReceived
											+
											(x - gp.x) *
											(twl->cyclesMaxReceived - twl->cyclesMinReceived) /
											gp.sx
											+
											twl->cyclesViewedDelta /
											2;

					twl->cyclesMax = twl->cyclesMaxLocked;
				}else{
					twl->dragAnchorCycles = twl->cyclesMax -
											twl->cyclesViewedDelta +
											(x - gp.x) * twl->cyclesViewedDelta / gp.sx;
				}
			}
			else if(twl->cyclesMaxLocked &&
					md->sx + md->xIndent > 1020 &&
					x >= md->xIndent + md->sx - BUTTON_GO_TO_END_OFFSET &&
					x < md->xIndent + md->sx - BUTTON_GO_TO_END_OFFSET + 15 &&
					md->y >= 2 &&
					md->y < 2 + 15)
			{
				toggleOpen = 0;

				twl->cyclesMaxLocked = 0;
				twl->cyclesMax = twl->cyclesMaxReceived;
			}
			else if(twle->twleType != TWLE_TIMER_ID_GROUP &&
					x >= 1000 - BUTTON_ENTIRE_HISTORY_OFFSET &&
					x < 1000 - 2 &&
					md->y >= 2 &&
					md->y < 2 + 15)
			{
				toggleOpen = 0;

				twleSetShowEntireHistory(twle, !twle->flags.showEntireHistory);
			}
			else if(twle->twleType == TWLE_TIMER_INSTANCE ||
					twle->twleType == TWLE_COPY)
			{
				if(twle->twleType == TWLE_COPY){
					twle = twle->copy.twleOriginal;
					assert(twle->twleType == TWLE_TIMER_INSTANCE);
				}

				if(twListIsLiveProcess(twl)){
					if(	(	wd->flags.breakPointsEnabled ||
							twle->timerInstance.flags.isBreakpoint) &&
						twle->timerInstance.twleTimerID &&
						x >= 1000 - BUTTON_BREAKPOINT_OFFSET &&
						x < 1000 - BUTTON_BREAKPOINT_OFFSET + 15 &&
						md->y >= 2 &&
						md->y < 2 + 15)
					{
						// Toggle breakpoint.

						U32						threadID;
						U32						instanceID;
						TempWindowListEntry*	twleThread = twle->twleThread;

						toggleOpen = 0;

						assert(twleThread->twleType == TWLE_THREAD);

						if(	autoTimerDecodedThreadGetID(twleThread->thread.dt, &threadID) &&
							autoTimerDecodedTimerInstanceGetInstanceID(twle->timerInstance.dti, &instanceID))
						{
							if(twl->atReader){
								autoTimerSetTimerBreakPoint(threadID,
															0,
															twle->timerInstance.instanceID,
															!twle->timerInstance.flags.isBreakpoint);
							}
							else if(twl->lleProcess){
								lleSendToHostSetTimerFlag(	twl->lleProcess,
															"TimerSetBreakpoint",
															threadID,
															twle->timerInstance.instanceID,
															!twle->timerInstance.flags.isBreakpoint);
							}
						}
					}
					else if(x >= 1000 - BUTTON_DISABLE_TIMER_OFFSET &&
							x < 1000 - BUTTON_DISABLE_TIMER_OFFSET + 15 &&
							md->y >= 2 &&
							md->y < 2 + 15)
					{
						// Toggle disabling the timer.

						U32						threadID;
						TempWindowListEntry*	twleThread = twle->twleThread;

						toggleOpen = 0;

						assert(twleThread->twleType == TWLE_THREAD);

						if(autoTimerDecodedThreadGetID(twleThread->thread.dt, &threadID)){
							S32 isOpen =	!twle->timerInstance.flags.forcedClosed &&
											(	!twle->timerInstance.flags.closedByDepth ||
												twle->timerInstance.flags.forcedOpen);

							if(twl->atReader){
								autoTimerSetTimerForcedOpen(threadID,
															0,
															twle->timerInstance.instanceID,
															!isOpen);
							}
							else if(twl->lleProcess){
								lleSendToHostSetTimerFlag(	twl->lleProcess,
															"TimerSetForcedOpen",
															threadID,
															twle->timerInstance.instanceID,
															!isOpen);
							}
						}
					}
				}
			}
		}

		if(toggleOpen){
			S32 isOpen;

			if(suiListEntryGetOpenState(le, &isOpen)){
				twleSetOpenState(twleOriginal, !isOpen);
			}
		}
	}
	else if(md->button & SUI_MBUTTON_RIGHT){
		if(hasGraphPos){
			if(	x >= gp.x &&
				x < gp.x + gp.sx &&
				y >= gp.y &&
				y <= gp.y + gp.sy)
			{
				// Clicking on the graph.

				TempWindowListEntry*	twleThread;
				U32						selectedFrameIndex = 0;

				twlUpdateSelectedFrame(twl, twle, x, &gp, &selectedFrameIndex);

				if(twleGetThread(twle, &twleThread)){
					if(!TRUE_THEN_RESET(twleThread->thread.locked.frameIndex)){
						twleThread->thread.locked.frameIndex = selectedFrameIndex;
						twleThread->thread.locked.cyclesBegin = twl->selectedCyclesBegin;
						twleThread->thread.locked.cyclesDelta = twl->selectedCyclesDelta;
					}
				}
			}
			else if(twle->twleType == TWLE_COPY){
				twle = twle->copy.twleOriginal;

				assert(twle->twleType == TWLE_TIMER_INSTANCE);

				twle->timerInstance.flags.selectedFromCopy = 1;

				twleScrollToMe(twle, twl);
			}
			else if(twle->twleType == TWLE_TIMER_ID){
				TempWindowListEntry* twleBest = NULL;

				EARRAY_CONST_FOREACH_BEGIN(twle->children, i, isize);
					TempWindowListEntry* twleChild = twle->children[i];
					
					assert(twleChild->twleType == TWLE_COPY);
					
					if(	!twleBest ||
						twleChild->copy.twleOriginal->timerInstance.cycles.total >
							twleBest->copy.twleOriginal->timerInstance.cycles.total)
					{
						twleBest = twleChild;
					}
				EARRAY_FOREACH_END;
				
				if(twleBest){
					twle = twleBest->copy.twleOriginal;

					assert(twle->twleType == TWLE_TIMER_INSTANCE);

					twle->timerInstance.flags.selectedFromCopy = 1;

					twleScrollToMe(twle, twl);
				}
			}
			else if(twle->twleType == TWLE_TIMER_INSTANCE){
				if(twle->timerInstance.twleCopy){
					twle->timerInstance.twleCopy->copy.flags.selectedFromOriginal = 1;

					twleScrollToMe(twle->timerInstance.twleCopy, twl);
				}
			}
		}
	}
	else if(md->button & SUI_MBUTTON_MIDDLE){
		//if(twle->twleType != TWLE_TIMER_ID_GROUP){
		//	twleSetShowEntireHistory(twle, !twle->flags.showEntireHistory);
		//}
	}
	
	return 1;
}

static S32 twleHandleMouseUp(	SUIWindow* w,
								TempWindowWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIListMsgEntryMouse*	md = msg->msgData;
	TempWindowList*				twl = msg->pipe.userPointer;
	TempWindowListEntry*		twle = md->le.userPointer;
	SUIListEntry*				le = md->le.le;
	S32							x = md->x + md->xIndent;

	if(TRUE_THEN_RESET(twl->dragAnchorCycles)){
		return 1;
	}

	return 1;
}

static void twleDestroy(TempWindowListEntry* twle,
						TempWindowList* twl)
{
	TempWindowListEntry* twleParent = twle->parent;

	suiListEntryDestroy(&twle->le);

	if(twleParent){
		if(eaFindAndRemove(&twleParent->children, twle) < 0){
			assert(0);
		}

		if(!eaSize(&twleParent->children)){
			eaDestroy(&twleParent->children);
		}
	}
	else if(eaFindAndRemove(&twl->twles, twle) >= 0 &&
			!eaSize(&twl->twles))
	{
		eaDestroy(&twl->twles);
	}

	switch(twle->twleType){
		xcase TWLE_THREAD:{
			autoTimerDecodedThreadSetUserPointer(twle->thread.dt, NULL);
			timerHistoryChunkListClear(&twle->thread.history);
		}
		xcase TWLE_TIMER_INSTANCE:{
			autoTimerDecodedTimerInstanceSetUserPointer(twle->timerInstance.dti, NULL);
			timerHistoryChunkListClear(&twle->timerInstance.history);
		}
		xcase TWLE_TIMER_ID:{
			timerHistoryChunkListClear(&twle->timerID.history);
		}
	}

	while(twle->children){
		twleDestroy(twle->children[0], twl);
	}

	SAFE_FREE(twle->text);
	SAFE_FREE(twle);

	suiCountDec(twleCount);
}

static S32 twleHandleDestroyed(	SUIWindow* w,
								TempWindowWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIListMsgEntryDestroyed*	md = msg->msgData;
	TempWindowListEntry*			twle = md->le.userPointer;
	TempWindowList*					twl = msg->pipe.userPointer;

	if(twl->twleUnderMouse == twle){
		twleSetChildIsUnderMouse(twl->twleUnderMouse->parent, 0, 0);
		twl->twleUnderMouse = NULL;
	}

	assert(twle->le == md->le.le);
	twle->le = NULL;

	//twleDestroy(twle, twl);

	return 1;
}

#if PRESSING_T_SPAWNS_TEST_THREAD
CrypticalSection csTest;

static U32 __stdcall testThread(void* unused){
	static U32 xShared;
	
	U32 count = 0;
	U32 total;
	U32 x = 0;
	
	EXCEPTION_HANDLER_BEGIN
	
	srand(GetCurrentThreadId());
	total = 100000 + rand() % 400000;

	while(count++ < total){
		autoTimerThreadFrameBegin(__FUNCTION__);
		csEnter(&csTest);
		x = ++xShared;
		csLeave(&csTest);
		autoTimerThreadFrameEnd();
	}
	
	printf("tid %d: x = %d\n", GetCurrentThreadId(), x);
	
	EXCEPTION_HANDLER_END
	
	return 0;
}
#endif

static U32 __stdcall testNothingThread(void* unused){
	U32 count = 0;
	U32 total;
	S32 disabled = 0;
	
	EXCEPTION_HANDLER_BEGIN
	
	total = 1000 + rand() % 40;

	autoTimerDisableRecursion(&disabled);
	while(count++ < total){
		U32 x = 0;
		S32 y = 500 + rand() % 500;
		y = SQR(y);
		Sleep(100);
		FOR_BEGIN(i, y);
			x += i;
		FOR_END;
	}
	autoTimerEnableRecursion(disabled);
	
	EXCEPTION_HANDLER_END
	
	return 0;
}

static void twYScaleChange(	TempWindowWD* wd,
							TempWindowList* twl,
							S32 increase)
{
	if(	!wd ||
		!twl)
	{
		return;
	}

	if(increase){
		if(wd->shownData == SHOWN_DATA_HIT_COUNT){
			twl->scaleCount *= 0.5f;
			MAX1(twl->scaleCount, 1.f);
		}else{
			twl->scaleCycles *= 0.5f;
		}
		suiWindowInvalidate(twl->wList);
	}else{
		if(wd->shownData == SHOWN_DATA_HIT_COUNT){
			twl->scaleCount *= 2.f;
		}else{
			twl->scaleCycles *= 2.f;
		}
		suiWindowInvalidate(twl->wList);
	}
}

static void twXScaleChange(	TempWindowWD* wd,
							TempWindowList* twl,
							S32 increase)
{
	if(	!wd ||
		!twl)
	{
		return;
	}

	if(increase){
		const U64 minValue = 100 * SQR((U64)1000);

		twl->cyclesViewedDelta *= 2;
		twl->cyclesViewedDelta /= 3;

		MAX1(twl->cyclesViewedDelta, minValue);

		suiWindowInvalidate(twl->wList);
	}else{
		const U64 maxValue = 100 * CUBE((U64)1000);

		twl->cyclesViewedDelta *= 3;
		twl->cyclesViewedDelta /= 2;

		MIN1(twl->cyclesViewedDelta, maxValue)

		suiWindowInvalidate(twl->wList);
	}
}

static S32 twHandleKeyDown(	SUIWindow* w,
							TempWindowWD* wd,
							const SUIWindowMsg* msg)
{
	const SUIWindowMsgKey*	md = msg->msgData;
	TempWindowList*			twlVisible = NULL;
	
	twGetVisibleList(wd, &twlVisible);

	// Check test keys first.

	switch(md->key){
		xcase SUI_KEY_S:{
			PerfInfoGuard* guard;
			
			PERFINFO_AUTO_START_GUARD("not broken", 1, &guard);
			{
				S32 count = rand() % 50;

				FOR_BEGIN(i, count);
					switch(rand() % 3){
						xcase 0: PERFINFO_AUTO_START("0", 1);
						xcase 1: PERFINFO_AUTO_START("1", 1);
						xcase 2: PERFINFO_AUTO_START("2", 1);
					}
				FOR_END;

				count = rand() % 50;
				FOR_BEGIN(i, count);
					PERFINFO_AUTO_STOP();
				FOR_END;
			}
			PERFINFO_AUTO_STOP_GUARD(&guard);
		}

		#if PRESSING_K_TEST_THREAD_SAMPLER
		xcase SUI_KEY_K:{
			ThreadSampler* ts;

			threadSamplerCreate(&ts, 10 * 1000 * 1000);
			threadSamplerSetThreadID(ts, GetCurrentThreadId());
			threadSamplerStart(ts);
			
			FOR_BEGIN(i, 1000);
				FOR_BEGIN(j, 1000);
					malloc(10);
				FOR_END;
				
				Sleep(1);
			FOR_END;
			
			threadSamplerStop(ts);
			threadSamplerReport(ts);
			threadSamplerDestroy(&ts);
		}
		#endif
		
		#if PRESSING_C_DOES_PROFILER_CONNECT
		xcase SUI_KEY_C:{
			profilerConnect("localhost");
			return 1;
		}
		#endif
		
		#if PRESSING_T_ADDS_TEST_LLE
			xcase SUI_KEY_T:{
				LeftListEntry* lle;

				lleCreate(	&lle,
							wd,
							wd->leftList.lleCommands,
							LLE_TEST,
							"Blah",
							30);
				
				return 1;
			}
		#elif PRESSING_T_SPAWNS_TEST_THREAD
			xcase SUI_KEY_T:{
				FOR_BEGIN(i, 2);
					_beginthreadex(NULL, 0, testThread, NULL, 0, NULL);
				FOR_END;

				return 1;
			}
		#elif 0
			xcase SUI_KEY_T:{
				_beginthreadex(NULL, 0, testNothingThread, NULL, 0, NULL);
				return 1;
			}
		#elif 0
			xcase SUI_KEY_T:{
				char* data;
				data = calloc(1, 32);
				free(data);
				free(data);
				return 1;
			}
		#elif 1
			xcase SUI_KEY_T:{
				ztest();
				return 1;
			}
		#endif
	}

	switch(md->key){
		xcase SUI_KEY_A:{
			twDoTextEntryPopup(wd, "Enter server name:", TWTE_ADD_SERVER, NULL);
			return 1;
		}
		
		xcase SUI_KEY_D:{
			if(md->modBits & SUI_KEY_MOD_CONTROL){
				EARRAY_CONST_FOREACH_BEGIN(wd->twls, i, isize);
					TempWindowList* twl = wd->twls[i];

					if(twl->flags.isVisible){
						twListDisconnectFromLiveProfile(twl);
						break;
					}
				EARRAY_FOREACH_END;
			}
		}

		xcase SUI_KEY_F:{
			twDoTextEntryPopup(	wd,
								"Enter timer name filter:",
								TWTE_SET_FILTER,
								SAFE_MEMBER(twlVisible, nameFilter));
			return 1;
		}

		xcase SUI_KEY_R:{
			if(md->modBits & SUI_KEY_MOD_CONTROL){
				lleProcessRecordToggle(SAFE_MEMBER(twlVisible, lleProcess), NULL);
				return 1;
			}
		}

		xcase SUI_KEY_W:{
			if(	md->modBits & SUI_KEY_MOD_CONTROL &
				!(md->modBits & SUI_KEY_MOD_SHIFT_ALT))
			{
				TempWindowList* twl;

				if(twGetVisibleList(wd, &twl)){
					twListDestroy(wd, &twl);

					return 1;
				}
			}
		}

		xcase SUI_KEY_TAB:{
			if(wd->leftList.widthTarget){
				wd->leftList.widthTarget = 0;
			}else{
				wd->leftList.widthTarget = LEFT_LIST_DEFAULT_WIDTH;
			}

			wd->leftList.timeStart = 0;

			if(	!wd->leftList.ph &&
				wd->leftList.width != wd->leftList.widthTarget)
			{
				suiWindowProcessingHandleCreate(w, &wd->leftList.ph);
			}

			return 1;
		}

		xcase SUI_KEY_SPACE:{
			if(twlVisible){
				twlVisible->flags.ignoreUpdates = !twlVisible->flags.ignoreUpdates;

				twReflow(wd);

				return 1;
			}
		}
		
		xcase SUI_KEY_MINUS:{
			twYScaleChange(wd, twlVisible, 0);
			return 1;
		}
		
		xcase SUI_KEY_PLUS:{
			twYScaleChange(wd, twlVisible, 1);
			return 1;
		}
		
		xcase SUI_KEY_LEFT_BRACKET:{
			twXScaleChange(wd, twlVisible, 0);
			return 1;
		}
		
		xcase SUI_KEY_RIGHT_BRACKET:{
			twXScaleChange(wd, twlVisible, 1);
			return 1;
		}

		//xcase SUI_KEY_Q:{
		//	return 1;
		//}
	}
	
	return 0;
}

static void twAddServer(TempWindowWD* wd,
						ProfilerConfigServer* pcs,
						const char* hostNameParam,
						S32 doConnect,
						U32 ip)
{
	char		hostNameBuffer[MAX_PATH];
	const char*	hostName;

	strcpy(hostNameBuffer, hostNameParam);

	removeTrailingWhiteSpaces(hostNameBuffer);
	hostName = removeLeadingWhiteSpaces(hostNameBuffer);

	if(hostName[0]){
		LeftListEntry* lle;

		lleCreate(&lle, wd, NULL, LLE_HOST, hostName, 0);
		
		eaPush(&wd->leftList.lleNetServers, lle);

		lle->host.hostName = strdup(hostName);
		lle->host.hostPort = CRYPTIC_PROFILER_PORT;
		lle->host.ip = ip;

		if(doConnect){
			lleHostConnect(lle);
		}

		if(!pcs){
			pcs = StructCreate(parse_ProfilerConfigServer);
			pcs->hostName = StructAllocString(hostName);
			pcs->ip = lle->host.ip;

			eaPush(&wd->config.cur.servers, pcs);

			twConfigWriteToDisk(wd);
		}else{
			if(pcs->computerName){
				lle->host.computerName = stringMakeLower(StructAllocString(pcs->computerName));
			}

			if(pcs->userName){
				lle->host.userName = stringMakeLower(StructAllocString(pcs->userName));
			}
		}

		lle->host.pcs = pcs;
		pcs->lle = lle;
	}
}

static void lleFolderChangeMsgHandler(const FolderChangeNotificationMsg* msg){
	LeftListEntry*	lleFolder = msg->userPointer;
	TempWindowWD*	wd = lleFolder->wdOwner;
	LeftListEntry*	lleFile = NULL;

	assert(lleFolder->lleType == LLE_RECORDINGS_FOLDER);

	EARRAY_CONST_FOREACH_BEGIN(lleFolder->children, i, isize);
		LeftListEntry* lle = lleFolder->children[i];

		if(!stricmp(lle->file.fileNameRelative, msg->pathChanged)){
			lleFile = lle;
			break;
		}
	EARRAY_FOREACH_END;

	switch(msg->msgType){
		xcase FCN_MSG_CREATED:{
			if(!lleFile){
				lleFolderAddFile(lleFolder, msg->pathChanged);
			}
		}
		
		xcase FCN_MSG_DELETED:{
			lleDestroy(&lleFile);
		}
		
		xcase FCN_MSG_MODIFIED:{
			lleFileSetText(lleFile);
		}
		
		xcase FCN_MSG_LOST_HANDLE:{
			fcnDestroy(&lleFolder->recordingFolder.fcn);
			
			lleFolder->recordingFolder.flags.scanned = 0;
			
			while(lleFolder->children){
				LeftListEntry* lle = lleFolder->children[0];
				
				lleDestroy(&lle);
			}
		}
	}

	suiWindowInvalidate(wd->leftList.w);
}

static void lleRecordingFolderScan(LeftListEntry* lle){
	assert(lle->lleType == LLE_RECORDINGS_FOLDER);
	
	if(lle->recordingFolder.flags.scanned){
		return;
	}
	
	if(!lle->recordingFolder.fcn){
		fcnCreate(	&lle->recordingFolder.fcn,
					lle->text,
					lle->wdOwner->iocp,
					lleFolderChangeMsgHandler,
					lle);
	}

	if(!lle->recordingFolder.fcn){
		lle->recordingFolder.flags.notFound = 1;
	}else{
		lle->recordingFolder.flags.scanned = 1;
		lle->recordingFolder.flags.notFound = 0;

		fileScanAllDataDirs(lle->text,
							addLocalFilesCallback,
							lle);
	}
}

static void twAddRecordingFolder(	TempWindowWD* wd,
									const char* folderPath,
									ProfilerConfigRecordingFolder* rf)
{
	LeftListEntry* lle;

	lleCreate(	&lle,
				wd,
				wd->leftList.lleRecordings,
				LLE_RECORDINGS_FOLDER,
				folderPath,
				18);

	forwardSlashes(lle->text);

	if(!rf){
		rf = StructCreate(parse_ProfilerConfigRecordingFolder);
		rf->path = StructAllocString(lle->text);

		eaPush(&wd->config.cur.rfs, rf);

		twConfigWriteToDisk(wd);
	}

	lle->recordingFolder.rf = rf;
	rf->lle = lle;

	//lleRecordingFolderScan(lle);
}

static S32 twMsgHandler(SUIWindow* w,
						TempWindowWD* wd,
						const SUIWindowMsg* msg)
{
	SUI_WM_DEFAULT_HANDLERS_BEGIN(w, msg, TempWindowWD, wd);
		SUI_WM_HANDLER(SUI_WM_CREATE,				twHandleCreate);
		SUI_WM_HANDLER(SUI_WM_DESTROY,				twHandleDestroy);
		SUI_WM_HANDLER(SUI_WM_MOUSE_DOWN,			twHandleMouseDown);
		SUI_WM_HANDLER(SUI_WM_MOUSE_MOVE,			twHandleMouseMove);
		SUI_WM_HANDLER(SUI_WM_MOUSE_DOUBLECLICK,	twHandleMouseDown);
		SUI_WM_HANDLER(SUI_WM_PROCESS,				twHandleProcess);
		SUI_WM_HANDLER(SUI_WM_DRAW,					twHandleDraw);
		SUI_WM_HANDLER(SUI_WM_KEY_DOWN,				twHandleKeyDown);

		SUI_WM_CASE(SUI_WM_MOUSE_LEAVE){
			if(TRUE_THEN_RESET(wd->flags.mouseOverSplitter)){
				suiWindowInvalidate(w);
			}
		}

		SUI_WM_CASE(SUI_WM_ADDED_TO_PARENT){
			twReflow(wd);
		}

		SUI_WM_CASE(SUI_WM_PARENT_SIZE_CHANGED){
			twReflow(wd);
		}

		SUI_WM_CASE(SUI_WM_MOUSE_UP){
			wd->flags.dragging = 0;
			suiWindowInvalidate(w);
		}
	SUI_WM_HANDLERS_END;

	SUI_WM_HANDLERS_BEGIN(w, msg, suiFrame, TempWindowWD, wd);
		SUI_WM_CASE(SUI_FRAME_MSG_DESTROYED){
			const SUIFrameMsgDestroyed* md = msg->msgData;
			TempWindowList*				twl = md->userPointer;

			//printf("Frame destroyed: 0x%8.8p.\n", twl);

			twListDestroy(wd, &twl);
		}
	SUI_WM_HANDLERS_END;

	SUI_WM_HANDLERS_BEGIN(w, msg, suiButton, TempWindowWD, wd);
		SUI_WM_CASE(SUI_BUTTON_MSG_BUTTON_PRESSED){
			TempWindowList* twlVisible = NULL;
			SUIWindow*		wWriter = msg->pipe.wWriter;
			S32				found = 1;

			twGetVisibleList(wd, &twlVisible);

			if(wWriter == wd->wButtonLeftListToggle){
				wd->leftList.widthTarget = wd->leftList.widthTarget ? 0 : LEFT_LIST_DEFAULT_WIDTH;
				wd->leftList.timeStart = 0;

				if(	!wd->leftList.ph &&
					wd->leftList.widthTarget != wd->leftList.width)
				{
					suiWindowProcessingHandleCreate(w, &wd->leftList.ph);
				}
			}
			else if(wWriter == wd->wButtonBreakPointToggle){
				char buffer[100];

				wd->flags.breakPointsEnabled = !wd->flags.breakPointsEnabled;

				sprintf(buffer,
						"Break:%s",
						wd->flags.breakPointsEnabled ? "ON" : "OFF");

				suiButtonSetText(wd->wButtonBreakPointToggle, buffer);

				twReflow(wd);
			}
			else if(wWriter == wd->wButtonUseFrameToggle){
				char buffer[100];

				wd->flags.useFrame = !wd->flags.useFrame;

				sprintf(buffer,
						"Frame:%s",
						wd->flags.useFrame ? "ON" : "OFF");

				suiButtonSetText(wd->wButtonUseFrameToggle, buffer);

				twReflow(wd);
			}
			else if(wWriter == wd->wButtonShownData){
				wd->shownData = (wd->shownData + 1) % SHOWN_DATA_COUNT;

				twReflow(wd);
			}
			else if(wWriter == wd->wButtonCyclesOnlyToggle){
				wd->flags.showCyclesOnly = !wd->flags.showCyclesOnly;

				twReflow(wd);
			}else{
				found = 0;

				EARRAY_CONST_FOREACH_BEGIN(wd->twls, i, isize);
					TempWindowList* twl = wd->twls[i];

					if(wWriter == twl->wTabButton){
						twSetCurrentList(wd, twl);
						found = 1;
						break;
					}
				EARRAY_FOREACH_END;
			}

			if(	!found &&
				twlVisible)
			{
				if(wWriter == wd->wButtonYScaleInc){
					twYScaleChange(wd, twlVisible, 1);
				}
				else if(wWriter == wd->wButtonYScaleDec){
					twYScaleChange(wd, twlVisible, 0);
				}
				else if(wWriter == wd->wButtonXScaleInc){
					twXScaleChange(wd, twlVisible, 1);
				}
				else if(wWriter == wd->wButtonXScaleDec){
					twXScaleChange(wd, twlVisible, 0);
				}
				else if(wWriter == wd->wButtonPauseToggle){
					twlVisible->flags.ignoreUpdates = !twlVisible->flags.ignoreUpdates;
					twReflow(wd);
				}
				else if(wWriter == wd->wButtonClear){
					twListClearData(twlVisible);
				}
				else if(wWriter == wd->wButtonIndentInc ||
						wWriter == wd->wButtonIndentDec)
				{
					S32 indent;

					if(suiListGetXIndentPerDepth(twlVisible->wList, &indent)){
						indent += wWriter == wd->wButtonIndentInc ? 1 : -1;
						MINMAX1(indent, 0, 20);

						suiListSetXIndentPerDepth(twlVisible->wList, indent);
					}
				}
			}
		}

		SUI_WM_CASE(SUI_BUTTON_MSG_BUTTON_PRESSED_RIGHT){
			SUIWindow* wWriter = msg->pipe.wWriter;

			if(	wWriter == wd->wButtonIndentInc ||
				wWriter == wd->wButtonIndentDec)
			{
				TempWindowList* twl;

				if(twGetVisibleList(wd, &twl)){
					suiListSetXIndentPerDepth(	twl->wList,
												wWriter == wd->wButtonIndentInc ?
													20 :
													0);
				}
			}else{
				EARRAY_CONST_FOREACH_BEGIN(wd->twls, i, isize);
					TempWindowList* twl = wd->twls[i];

					if(wWriter == twl->wTabButton){
						twListDestroy(wd, &twl);
						break;
					}
				EARRAY_FOREACH_END;
			}
		}
		
		SUI_WM_CASE(SUI_BUTTON_MSG_BUTTON_PRESSED_MIDDLE){
			SUIWindow* wWriter = msg->pipe.wWriter;

			if(0){
			}else{
				EARRAY_CONST_FOREACH_BEGIN(wd->twls, i, isize);
					TempWindowList* twl = wd->twls[i];

					if(wWriter == twl->wTabButton){
						twListDisconnectFromLiveProfile(twl);
						break;
					}
				EARRAY_FOREACH_END;
			}
		}

		SUI_WM_CASE(SUI_BUTTON_MSG_DRAW){
			const SUIButtonMsgDraw* md = msg->msgData;

			if(msg->pipe.wWriter == wd->wButtonShownData){
				U32 rgbActive = ARGB_CYCLES_ACTIVE;
				U32 rgbBlocking = 0x4444ff;
				U32 rgbTop;
				U32 rgbBottom;

				switch(wd->shownData){
					xcase SHOWN_DATA_CPU_BLOCKING_THEN_ACTIVE:{
						rgbTop = rgbActive;
						rgbBottom = rgbBlocking;
					}
					xcase SHOWN_DATA_CPU_BLOCKING:{
						rgbTop = rgbBottom = rgbBlocking;
					}
					xcase SHOWN_DATA_CPU_ACTIVE_THEN_BLOCKING:{
						rgbTop = rgbBlocking;
						rgbBottom = rgbActive;
					}
					xcase SHOWN_DATA_CPU_ACTIVE:{
						rgbTop = rgbBottom = rgbActive;
					}
					xcase SHOWN_DATA_HIT_COUNT:{
						rgbTop = rgbBottom = 0xff00ff;
					}
				}

				suiDrawFilledRect(	md->dc,
									0,
									0,
									md->sx,
									md->sy / 2,
									0xff000000 | rgbTop);

				suiDrawFilledRect(	md->dc,
									0,
									0 + md->sy / 2,
									md->sx,
									md->sy - md->sy / 2,
									0xff000000 | rgbBottom);

				suiDrawRect(md->dc,
							0,
							0,
							md->sx,
							md->sy,
							1,
							md->flags.isUnderMouse ?
								0xffaaaaff :
								0xff000000);

				if(rgbTop != rgbBottom){
					suiDrawFilledRect(	md->dc,
										0,
										md->sy / 2 - 1,
										md->sx,
										1,
										md->flags.isUnderMouse ?
											0xffaaaaff :
											0xff000000);
				}
			}
			else if(msg->pipe.wWriter == wd->wButtonCyclesOnlyToggle){
				if(wd->flags.showCyclesOnly){
					suiDrawFilledRect(	md->dc,
										0,
										0,
										md->sx,
										md->sy,
										ARGB_CYCLES_ACTIVE);
				}else{
					suiDrawFilledRect(	md->dc,
										0,
										0,
										md->sx / 2,
										md->sy,
										ARGB_CYCLES_ACTIVE);
					
					suiDrawFilledRect(	md->dc,
										md->sx / 2,
										0,
										md->sx - md->sx / 2,
										md->sy,
										ARGB_NON_CYCLES);
				}

				suiDrawRect(md->dc,
							0,
							0,
							md->sx,
							md->sy,
							1,
							md->flags.isUnderMouse ?
								0xffaaaaff :
								0xff000000);

				if(!wd->flags.showCyclesOnly){
					suiDrawFilledRect(	md->dc,
										md->sx / 2,
										0,
										1,
										md->sy,
										md->flags.isUnderMouse ?
											0xffaaaaff :
											0xff000000);
				}
			}
		}
	SUI_WM_HANDLERS_END;

	SUI_WM_GROUP_BEGIN(w, msg, suiList, TempWindowWD, wd);
		if(msg->pipe.wWriter == wd->leftList.w){
			// Left-side list of stuff.

			SUI_WM_GROUP_HANDLERS_BEGIN(w, msg, TempWindowWD, wd);
				SUI_WM_HANDLER(SUI_LIST_MSG_ENTRY_DRAW,			lleHandleDraw);
				SUI_WM_HANDLER(SUI_LIST_MSG_ENTRY_MOUSE_DOWN,	lleHandleMouseDown);
				SUI_WM_HANDLER(SUI_LIST_MSG_ENTRY_DESTROYED,	lleHandleDestroyed);

				SUI_WM_CASE(SUI_LIST_MSG_ENTRY_MOUSE_ENTER){
					suiWindowInvalidate(wd->leftList.w);
				}

				SUI_WM_CASE(SUI_LIST_MSG_ENTRY_MOUSE_LEAVE){
					suiWindowInvalidate(wd->leftList.w);
				}
			SUI_WM_GROUP_HANDLERS_END;
		}else{
			// Main process view.

			SUI_WM_GROUP_HANDLERS_BEGIN(w, msg, TempWindowWD, wd);
				SUI_WM_HANDLER(SUI_LIST_MSG_ENTRY_MOUSE_ENTER,	twleHandleMouseEnter);
				SUI_WM_HANDLER(SUI_LIST_MSG_ENTRY_MOUSE_LEAVE,	twleHandleMouseLeave);
				SUI_WM_HANDLER(SUI_LIST_MSG_ENTRY_MOUSE_MOVE,	twleHandleMouseMove);
				SUI_WM_HANDLER(SUI_LIST_MSG_ENTRY_DRAW,			twleHandleDraw);
				SUI_WM_HANDLER(SUI_LIST_MSG_ENTRY_MOUSE_DOWN,	twleHandleMouseDown);
				SUI_WM_HANDLER(SUI_LIST_MSG_ENTRY_MOUSE_UP,		twleHandleMouseUp);
				SUI_WM_HANDLER(SUI_LIST_MSG_ENTRY_DESTROYED,	twleHandleDestroyed);
			SUI_WM_GROUP_HANDLERS_END;
		}
	SUI_WM_GROUP_END;

	SUI_WM_HANDLERS_BEGIN(w, msg, textEntryPopup, TempWindowWD, wd);
		SUI_WM_CASE(TEXT_ENTRY_POPUP_MSG_CANCEL){
		}

		SUI_WM_CASE(TEXT_ENTRY_POPUP_MSG_OK){
			const TextEntryPopupMsg*	md = msg->msgData;

			switch(wd->textEntryType){
				xcase TWTE_ADD_SERVER:{
					twAddServer(wd, NULL, md->inputText, 1, 0);
				}

				xcase TWTE_SET_FILTER:{
					TempWindowList* twl;

					if(twGetVisibleList(wd, &twl)){
						suiWindowInvalidate(twl->wList);

						estrCopy2(&twl->nameFilter, md->inputText);

						EARRAY_CONST_FOREACH_BEGIN(twl->twles, i, isize);
							TempWindowListEntry* twleThread = twl->twles[i];
							TempWindowListEntry* twleTimerIDs;

							if(twleThread->twleType != TWLE_THREAD){
								continue;
							}

							twleThread->thread.flags.hasVisibleTimerIDs = 0;

							twleTimerIDs = twleThread->thread.twle.timerIDs;

							assert(twleTimerIDs->twleType == TWLE_TIMER_ID_GROUP);

							EARRAY_CONST_FOREACH_BEGIN(twleTimerIDs->children, j, jsize);
								TempWindowListEntry*	twleTimerID = twleTimerIDs->children[j];
								S32						hidden;

								assert(twleTimerID->twleType == TWLE_TIMER_ID);

								hidden = !twleNamePassesListFilter(twleTimerID, twl);

								if(twleTimerID->le){
									twleSetHiddenState(twleTimerID, hidden);

									if(!hidden){
										twleThread->thread.flags.hasVisibleTimerIDs = 1;
									}
								}
							EARRAY_FOREACH_END;
						EARRAY_FOREACH_END;
					}
				}

				xcase TWTE_ADD_RECORDING_FOLDER:{
					if(SAFE_DEREF(md->inputText)){
						twAddRecordingFolder(wd, md->inputText, NULL);
					}
				}
			}
		}
	SUI_WM_HANDLERS_END;

	return 1;
}

static U32 __stdcall twThreadMain(void* unused){
	SUIMainWindow* mws[1];

	EXCEPTION_HANDLER_BEGIN

	PERFINFO_AUTO_START("startup", 1);
		ARRAY_FOREACH_BEGIN(mws, i);
		{
			static U32		count;
			SUIWindow*		wTemp;
			char			title[100];
			U32				curCount;
			SUIMainWindow*	mw;

			curCount = InterlockedIncrement(&count);

			sprintf(title,
					"Profiler: pid %d, tid %d, Window #%d",
					GetCurrentProcessId(),
					GetCurrentThreadId(),
					curCount);

			if(!suiMainWindowCreateBasic(	&mw,
											title,
											0
												| (1 ? SUI_MWSTYLE_TASKBAR_BUTTON : 0)
												| (1 ? SUI_MWSTYLE_BORDER : 0)
											))
			{
				continue;
			}

			mws[i] = mw;
			
			{
				TempWindowCreateParams cp = {0};

				cp.mw = mw;

				if(suiWindowCreate(&wTemp, NULL, twMsgHandler, &cp)){
					suiMainWindowAddChild(mws[i], wTemp);
				}
			}
		}
		ARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();

	{
		S32 done = 0;

		while(!done){
			autoTimerThreadFrameBegin(__FUNCTION__);

			done = 1;

			ARRAY_FOREACH_BEGIN(mws, i);
			{
				if(suiMainWindowProcess(mws[i])){
					done = 0;
				}else{
					mws[i] = NULL;
				}
			}
			ARRAY_FOREACH_END;

			autoTimerThreadFrameEnd();
		}
	}

	EXCEPTION_HANDLER_END

	return 0;
}

static HANDLE newTempWindowThread(void){
	return (HANDLE)_beginthreadex(NULL, 0, twThreadMain, NULL, 0, NULL);
}

static S32 showTempWindow(void){
	HANDLE hThread[1];

	ARRAY_FOREACH_BEGIN(hThread, i);
		hThread[i] = newTempWindowThread();
	ARRAY_FOREACH_END;

	while(1){
		S32 allThreadsFinished = 1;
		U32 msToSleep = 1000;

		autoTimerThreadFrameBegin("main");

		PERFINFO_AUTO_START("waitForThreads", 1);
		ARRAY_FOREACH_BEGIN(hThread, i);
			U32 waitResult;
			if(!hThread[i]){
				continue;
			}
			WaitForSingleObjectWithReturn(hThread[i], msToSleep, waitResult);
			switch(waitResult){
				xcase WAIT_OBJECT_0:
				acase WAIT_ABANDONED:{
					hThread[i] = 0;
				}
				xdefault:{
					allThreadsFinished = 0;
				}
			}
			msToSleep = 0;
		ARRAY_FOREACH_END;
		PERFINFO_AUTO_STOP();

		if(allThreadsFinished){
			break;
		}

		utilitiesLibOncePerFrame(0, 0);

		autoTimerThreadFrameEnd();
	}

	return 1;
}

static void processCmdLine(char* cmdLineText){
	int argc = 0;
	char *args[1000];
	char **argv = args;
	char buf[1000]={0};
	args[0] = getExecutableName();
	argc = 1 + tokenize_line_quoted_safe(cmdLineText,&args[1],ARRAY_SIZE(args)-1,0);
	cmdParseCommandLine(argc, argv);
}

static void setupConsoleWindow(void){
	//newConsoleWindow();
	showConsoleWindow();
	consoleUpSize(140, 9999);
	setConsoleTitle(STACK_SPRINTF("Process %d", GetCurrentProcessId()));
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'P', 0x404040);
}

S32 WINAPI WinMain(	HINSTANCE hInstance,
					HINSTANCE hPrevInstance,
					char* cmdLineText,
					S32 nShowCmd)
{
	void runProfilerJunk(void);

	EXCEPTION_HANDLER_BEGIN
	gimmeDLLDisable(1);
	DO_AUTO_RUNS;

	#if 0
	{
		void setFreeVirtualMemory(int mb);
		setFreeVirtualMemory(200);
	}
	#endif

	//timeBeginPeriod(1);

	setAssertMode(0);
	fileDisableAutoDataDir();
	memMonitorInit();
	setupConsoleWindow();
	processCmdLine(cmdLineText);
	utilitiesLibStartup();
	autoTimerInit();

	//doDeviceThing();

	//SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);

	//runProfilerJunk();

	//ztest();
	showTempWindow();
	
	//Sleep(1500);

	EXCEPTION_HANDLER_END

	return 0;
}

#include "AutoGen/Profiler_c_ast.c"