/*
 * DeepSpaceServer
 */

#include "BlockEarray.h"
#include "DeepSpaceReports.h"
#include "DeepSpaceServer.h"
#include "DeepSpaceServer_c_ast.h"
#include "DeepSpaceServer_h_ast.h"
#include "estring.h"
#include "file.h"
#include "FolderCache.h"
#include "GlobalComm.h"
#include "GlobalTypes.h"
#include "logging.h"
#include "MemoryMonitor.h"
#include "ServerLib.h"
#include "StayUp.h"
#include "sock.h"
#include "StashTable.h"
#include "StringCache.h"
#include "structNet.h"
#include "sysutil.h"
#include "TimedCallback.h"
#include "timing.h"
#include "timing_profiler.h"
#include "timing_profiler_interface.h"
#include "utilitieslib.h"
#include "winutil.h"
#include "WorkerThread.h"

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

// Protocol version
#define DEEPSPACE_SERVER_PROTOCOL 1

// Server packet commands
enum
{
	TO_DSS_HELLO = DEPRECATED_SHAREDCMD_MAX,
	FROM_DSS_HELLO,
	TO_DSS_ADVERTISE,
	TO_DSS_REQUEST_UNIQUEMACHINE,
	TO_DSS_UNIQUEMACHINE,
};

AUTO_ENUM;
typedef enum DeepSpaceNetworkEvent {
	DeepSpaceNetworkEvent_CrypticTorrent_Started = 1,
	DeepSpaceNetworkEvent_CrypticTorrent_Exit,
	DeepSpaceNetworkEvent_CrypticTorrent_Periodic,
} DeepSpaceNetworkEvent;

// A DSN client
typedef struct DsnClient {
	char *buffer;		// Input buffer
} DsnClient;

// Packet header
typedef struct DsnTorrentHeader {
	char magic[4];
	int length;
	U64 packet_id;
	char client_type[4];
	char machine_id[32];
	char info_hash[32];
	unsigned lcid;
	unsigned client_version;
	unsigned event;
	char product[1];
} DsnTorrentHeader;

// Server config file
AUTO_STRUCT;
typedef struct DSNServerConfig {
	int iServerId;											// Server ID (should be unique)
	int iOverridePort;										// Listen on a non-default port
	INT_EARRAY pExtraPort;									// Extra ports to listen on
	STRING_EARRAY ppConnectToServer;						// Connect to these servers
	bool bRequestDebugging;									// If true, write deepspaceserver_send.log and deepspaceserver_recv.log

	// Server links: Both of these match ppConnectToServer.
	CommConnectFSM **ppConnectToServerFsm;	NO_AST
	NetLink **ppConnectToServerLink;		NO_AST
} DSNServerConfig;

// Server configuration
static DSNServerConfig sConfig = {0};

// All data
DSNDatabase sDatabase = {0};

// Look up table of machines in the database
StashTable machine_stash;  // machine_id[8] (fixed) -> index (int)

// Thread for saving the database
static WorkerThread *save_thread = NULL;

// Connected servers
static NetLink **sConnectedServers = NULL;

// Server statistics: machines requested from another peer server
static int sServerStatsMachinesReceived = 0;

// Server statistics: machines send to another peer server
static int sServerStatsMachinesSent = 0;

// Populate machine_stash.
static void InitializeMachineStash()
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	// If there was a previous StashTable, destroy it.
	if (machine_stash)
		stashTableDestroy(machine_stash);

	// Create a new StashTable.
	machine_stash = stashTableCreateFixedSize(beaSize(&sDatabase.machines), 32);

	// Add each entry.
	for (i = 0; i != beaSize(&sDatabase.machines); ++i)
	{
		bool success = stashAddInt(machine_stash, sDatabase.machines[i].machine_id, i, false);
		devassert(success);
	}

	// Note the machines pointer.
	sDatabase.old_machines = sDatabase.machines;

	PERFINFO_AUTO_STOP_FUNC();
}

// Find the machine that matches a particular machine ID.
static UniqueMachine *FindMachine(U32 machine_id[8], bool create)
{
	UniqueMachine *machine;
	int index;
	bool success;

	// Create machine StashTable.
	if (!machine_stash)
		InitializeMachineStash();

	// See if we already have it, and if so, return it.
	success = stashFindInt(machine_stash, machine_id, &index);
	if (success)
	{
		assert(index < beaSize(&sDatabase.machines));
		return sDatabase.machines + index;
	}

	// If we aren't being asked to make it, return.
	if (!create)
		return NULL;

	// We don't have it: make a new entry!
	machine = beaPushEmptyStruct(&sDatabase.machines, parse_UniqueMachine);
	memcpy(machine->machine_id, machine_id, sizeof(machine->machine_id));

	// See if we need to rehash, or if we can just add this stash entry.
	if (sDatabase.machines == sDatabase.old_machines)
	{
		success = stashAddInt(machine_stash, machine->machine_id, machine - sDatabase.machines, false);
		devassert(success);
	}
	else
	{
		// Rehash.
		// This is necessary because all of the pointers have changed.
		InitializeMachineStash();
	}
	
	return machine;
}

// Send a machine to a link.
static void DSS_Send(NetLink *link, UniqueMachine *machine)
{
	Packet *pak = pktCreate(link, TO_DSS_UNIQUEMACHINE);
	ParserSendStructSafe(parse_UniqueMachine, pak, machine);
	pktSend(&pak);
}

// Send a machine to all connected servers.
static void DSS_SendAll(UniqueMachine *machine)
{
	EARRAY_CONST_FOREACH_BEGIN(sConnectedServers, i, n);
	{
		DSS_Send(sConnectedServers[i], machine);
	}
	EARRAY_FOREACH_END;
}

// Return true if this event is present in any of the event arrays.
static bool AlreadyHaveEvent(UniqueMachine *machine, U64 event_id)
{
	int i;
	for (i = 0; i != beaSize(&machine->started_events); ++i)
		if (machine->started_events[i].event.packet_id == event_id)
			return true;
	for (i = 0; i != beaSize(&machine->exit_events); ++i)
		if (machine->exit_events[i].event.packet_id == event_id)
			return true;
	for (i = 0; i != beaSize(&machine->periodic_events); ++i)
		if (machine->periodic_events[i].event.packet_id == event_id)
			return true;
	return false;
}

// Process a DSN packet.
static void DSN_Process(DsnClient *client, const char *packet, int packet_size, U32 uIp)
{
	const DsnTorrentHeader *header;
	const char *payload;
	int payload_size;
	U32 machine_id[8];
	UniqueMachine *machine;
	BasicEvent basic;
	char ip[MAX_IP_STR];

	PERFINFO_AUTO_START_FUNC();

	// Get header.
	header = (const DsnTorrentHeader *)packet;

	// Get machine ID.
	memcpy(machine_id, header->machine_id, sizeof(machine_id));

	// Get BasicEvent fields.
	basic.when = timeSecondsSince2000();
	basic.packet_id = header->packet_id;
	basic.uIp = uIp;
	basic.origin_server = sConfig.iServerId;
	basic.client_type = !memcmp(header->client_type, "CDT", 4) ? DsnClientType_CrypticTorrent : DsnClientType_Unknown;
	memcpy(basic.info_hash, header->info_hash, sizeof(basic.info_hash));
	basic.lcid = header->lcid;
	basic.client_version = header->client_version;

	// Look up machine.
	machine = FindMachine(machine_id, true);

	// Find payload.
	payload = header->product + strlen(header->product) + 1;
	if (payload - packet > packet_size)
	{
		servLog(LOG_BADCLIENT, "DsnBadPayload", "ip %s", GetIpStr(uIp, SAFESTR(ip)));
		return;
	}
	payload_size = packet_size - (payload - packet);

	// Save product.
	basic.product = allocAddString(header->product);

	// Reject events we already know about.
	if (AlreadyHaveEvent(machine, basic.packet_id))
	{
		PERFINFO_AUTO_START_FUNC();
		return;
	}

	switch (header->event)
	{
		case DeepSpaceNetworkEvent_CrypticTorrent_Started: {
			StartedEvents *started_event = beaPushEmptyStruct(&machine->started_events, parse_StartedEvents);
			started_event->event = basic;
		} break;

		case DeepSpaceNetworkEvent_CrypticTorrent_Exit: {
			ExitEvents *exit_event;
			int scanned = -1;
			int started_download = 0;
			int got_bytes = 0;
			int download_percent = 0;
			int download_finished = 0;
			int installer_ran = 0;

			if (payload_size)
			{
				scanned = sscanf(payload, "%d %d %d %d %d", &started_download, &got_bytes, &download_percent, &download_finished, &installer_ran);
			}
			if (scanned != 5)
			{
				servLog(LOG_BADCLIENT, "DsnBadExitPayload", "ip %s scanned %d", GetIpStr(uIp, SAFESTR(ip)), scanned);
				return;
			}

			exit_event = beaPushEmptyStruct(&machine->exit_events, parse_ExitEvents);
			exit_event->event = basic;
			exit_event->bStartedDownload = started_download;
			exit_event->bGotBytes = got_bytes;
			exit_event->iDownloadPercent = download_percent;
			exit_event->bDownloadFinished = download_finished;
			exit_event->bInstallerRan = installer_ran;
		} break;

		case DeepSpaceNetworkEvent_CrypticTorrent_Periodic: {
			PeriodicEvents *periodic_event;
			int size;
			int final;

			if (payload_size < 9)
				return;
			size = *(int *)payload;
			final = *(int *)(payload + 4);

			if (size != sizeof(periodic_event->blob))
				return;
			if (payload_size != size + 8 + 1)
				return;

			periodic_event = beaPushEmptyStruct(&machine->periodic_events, parse_PeriodicEvents);
			periodic_event->event = basic;
			memcpy(&periodic_event->blob, payload + 8, sizeof(periodic_event->blob));
			
		} break;
	}

	DSS_SendAll(machine);

	PERFINFO_AUTO_STOP_FUNC();
}

// Received data from client
static void DSN_ServingMsg(Packet *pkt, int cmd, NetLink *link, void *state)
{
	DsnClient *client = state;
	char *data = pktGetStringRaw(pkt);
	U32 len = pktGetSize(pkt);
	char ip[MAX_IP_STR];
	unsigned length;
	Packet *pak;

	// Concatenate data to input buffer.
	estrConcat(&client->buffer, data, len);
	
	// Drop the link if it looks like it's invalid data.
	if (estrLength(&client->buffer) < 4)
		return;
	if (memcmp(client->buffer, "DSN", 4))
	{
		servLog(LOG_BADCLIENT, "DsnInvalidMagic", "ip %s magic %u", linkGetIpStr(link, SAFESTR(ip)), *(int *)client->buffer);
		linkRemove_wReason(&link, "Invalid magic received");
		return;
	}

	// Get length of packet.
	if (estrLength(&client->buffer) < 8)
		return;
	length = *(int *)(client->buffer+4);

	// Make sure length is sane.
	if (estrLength(&client->buffer) < 90 || estrLength(&client->buffer) > 4096)
	{
		servLog(LOG_BADCLIENT, "DsnInvalidPacketSize", "ip %s size %u", linkGetIpStr(link, SAFESTR(ip)), estrLength(&client->buffer));
		linkRemove_wReason(&link, "Invalid packet size");
		return;
	}

	// Wait for entire packet to arrive.
	if (estrLength(&client->buffer) < length)
		return;

	// Process the packet.
	DSN_Process(client, client->buffer, length, linkGetIp(link));

	// Notify client that we got it.
	pak = pktCreateRaw(link);
	pktSendStringRaw(pak, ".");
	pktSendRaw(&pak);

	// Trim the buffer.
	estrRemove(&client->buffer, 0, length);
}

// A client has connected.
static int DSN_ServingConnect(NetLink *link, void *state)
{
	DsnClient *client = state;
	char ip[MAX_IP_STR];

	servLog(LOG_CLIENTSERVERCOMM, "DsnClientConnect", "ip %s", linkGetIpStr(link, SAFESTR(ip)));

	return 1;
}

// A client has disconnected.
static int DSN_ServingDisconnect(NetLink *link, void *state)
{
	DsnClient *client = state;
	char *reason = NULL;
	U32 error_code;
	char ip[MAX_IP_STR];

	// TODO: Report something if client disconnects without saying anything?

	// Get disconnect reason.
	estrStackCreate(&reason);
	linkGetDisconnectReason(link, &reason);
	error_code = linkGetDisconnectErrorCode(link);

	// Log it.
	servLog(LOG_CLIENTSERVERCOMM, "DsnClientDisconnect", "ip %s reason \"%s\" code %d", linkGetIpStr(link, SAFESTR(ip)), reason, error_code);
	estrDestroy(&reason);

	return 1;
}

// Tell a link about all of the machines we have.
static void DSS_Advertise(NetLink *link)
{
	Packet *pak;
	U32 crc;
	int i;

	PERFINFO_AUTO_START_FUNC();

	pak = pktCreate(link, TO_DSS_ADVERTISE);

	// Send database schema CRC.
	crc = ParseTableCRC(parse_DSNDatabase, NULL, 0);
	pktSendU32(pak, crc);

	// Send number of machines in database.
	pktSendU32(pak, beaSize(&sDatabase.machines));

	// Send every machine ID and its machine's data CRC.
	for (i = 0; i != beaSize(&sDatabase.machines); ++i)
	{
		pktSendBytes(pak, sizeof(sDatabase.machines[i].machine_id), sDatabase.machines[i].machine_id);
		pktSendU32(pak, StructCRC(parse_UniqueMachine, &sDatabase.machines[i]));
	}

	pktSend(&pak);

	PERFINFO_AUTO_STOP_FUNC();
}

// Merge in machine data to our copy.
static void MergeMachine(UniqueMachine *new_machine)
{
	UniqueMachine *machine;
	int i, j;

	PERFINFO_AUTO_START_FUNC();

	// If the machine doesn't already exist, use the new struct.
	machine = FindMachine(new_machine->machine_id, false);
	if (!machine)
	{
		machine = FindMachine(new_machine->machine_id, true);
		StructCopy(parse_UniqueMachine, new_machine, machine, 0, 0, 0);
	}

	// Merge started events.
	for (i = 0; i != beaSize(&new_machine->started_events); ++i)
	{
		bool found_it = false;

		// See if we already have it.
		for (j = 0; j != beaSize(&machine->started_events); ++j)
		{
			if (new_machine->started_events[i].event.packet_id == machine->started_events[j].event.packet_id)
			{
				// Resolve the conflict by using the lower-ordered one.
				int compare = StructCompare(parse_StartedEvents, &new_machine->started_events[i], &machine->started_events[j], 0, 0, 0);  // FIXME consistify with below
				if (compare < 1)
				{
					StructDeInit(parse_StartedEvents, &machine->started_events[j]);
					StructCopy(parse_StartedEvents, &new_machine->started_events[i], &machine->started_events[j], 0, 0, 0);
				}
				found_it = true;
				break;
			}
		}

		// If we don't have it, add it.
		if (!found_it)
		{
			StartedEvents *started_event = beaPushEmptyStruct(&machine->started_events, parse_StartedEvents);
			StructCopy(parse_StartedEvents, &new_machine->started_events[i], started_event, 0, 0, 0);
		}
	}

	// Merge exit events.
	for (i = 0; i != beaSize(&new_machine->exit_events); ++i)
	{
		bool found_it = false;

		// See if we already have it.
		for (j = 0; j != beaSize(&machine->exit_events); ++j)
		{
			if (new_machine->exit_events[i].event.packet_id == machine->exit_events[j].event.packet_id)
			{
				// Resolve the conflict by using the lower-ordered one.
				int compare = StructCompare(parse_ExitEvents, &new_machine->exit_events[i], &machine->exit_events[j], 0, 0, 0);
				if (compare < 1)
				{
					StructDeInit(parse_ExitEvents, &machine->exit_events[j]);
					StructCopy(parse_ExitEvents, &new_machine->exit_events[i], &machine->exit_events[j], 0, 0, 0);
				}
				found_it = true;
				break;
			}
		}

		// If we don't have it, add it.
		if (!found_it)
		{
			ExitEvents *exit_event = beaPushEmptyStruct(&machine->exit_events, parse_ExitEvents);
			StructCopy(parse_ExitEvents, &new_machine->exit_events[i], exit_event, 0, 0, 0);
		}
	}

	// Merge periodic events.
	for (i = 0; i != beaSize(&new_machine->periodic_events); ++i)
	{
		bool found_it = false;

		// See if we already have it.
		for (j = 0; j != beaSize(&machine->periodic_events); ++j)
		{
			if (new_machine->periodic_events[i].event.packet_id == machine->periodic_events[j].event.packet_id)
			{
				// Resolve the conflict by using the lower-ordered one.
				int compare = StructCompare(parse_PeriodicEvents, &new_machine->periodic_events[i], &machine->periodic_events[j], 0, 0, 0);
				if (compare < 1)
				{
					StructDeInit(parse_PeriodicEvents, &machine->periodic_events[j]);
					StructCopy(parse_PeriodicEvents, &new_machine->periodic_events[i], &machine->periodic_events[j], 0, 0, 0);
				}
				found_it = true;
				break;
			}
		}

		// If we don't have it, add it.
		if (!found_it)
		{
			PeriodicEvents *periodic_event = beaPushEmptyStruct(&machine->periodic_events, parse_PeriodicEvents);
			StructCopy(parse_PeriodicEvents, &new_machine->periodic_events[i], periodic_event, 0, 0, 0);
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Write out the machine requests that we're sending.
static void DSS_DebugSendRequest(U32 machine_id[8], UniqueMachine *machine)
{
	char *string = NULL;

	if (!sConfig.bRequestDebugging)
		return;

	filelog_printf("deepspaceserver_request_send.log", "send_request %d %d %d %d %d %d %d %d",
		machine_id[0], machine_id[1], machine_id[2], machine_id[3], machine_id[4], machine_id[5], machine_id[6], machine_id[7]);

	estrStackCreate(&string);
	if (machine)
		ParserWriteText(&string, parse_UniqueMachine, machine, 0, 0, 0);
	else
		estrCopy2(&string, "(null)\n");
	filelog_printf("deepspaceserver_send.log", "%s", string);
	estrDestroy(&string);
}

// Write out the machine requests that we're receiving.
static void DSS_DebugReceiveRequest(U32 machine_id[8], UniqueMachine *machine)
{
	char *string = NULL;

	if (!sConfig.bRequestDebugging)
		return;

	filelog_printf("deepspaceserver_request_recv.log", "recv_request %d %d %d %d %d %d %d %d",
		machine_id[0], machine_id[1], machine_id[2], machine_id[3], machine_id[4], machine_id[5], machine_id[6], machine_id[7]);

	estrStackCreate(&string);
	if (machine)
		ParserWriteText(&string, parse_UniqueMachine, machine, 0, 0, 0);
	else
		estrCopy2(&string, "(null)\n");
	filelog_printf("deepspaceserver_recv.log", "%s", string);
	estrDestroy(&string);
}

// Got a Cryptic packet from another Deep Space Server.
static void DSS_ServingMsg(Packet *pkt, int cmd, NetLink *link, void *state)
{
	Packet *reply;
	char ip[MAX_IP_STR];

	switch (cmd)
	{
		// A server just connected to us.
		case TO_DSS_HELLO:
		{
			int version = pktGetU32(pkt);
			int id;

			// Check version.
			if (version != DEEPSPACE_SERVER_PROTOCOL)
			{
				AssertOrAlert("DUPLICATE_ID", "Connection from incompatible protocol version %d", version);
				linkFlushAndClose(&link, "version");
				return;
			}

			// Check ID.
			id = pktGetU32(pkt);
			if (id == sConfig.iServerId)
			{
				AssertOrAlert("DUPLICATE_ID", "Connection from duplicate id %d", id);
				linkFlushAndClose(&link, "duplicate");
				return;
			}

			// Send response.
			reply = pktCreate(link, FROM_DSS_HELLO);
			pktSendU32(reply, sConfig.iServerId);
			pktSend(&reply);
		}
		break;

		// Reply to a hello.
		case FROM_DSS_HELLO:
		{
			int id = pktGetU32(pkt);
			if (id == sConfig.iServerId)
			{
				AssertOrAlert("DUPLICATE_ID", "Connection to duplicate id %d", id);
				linkFlushAndClose(&link, "duplicate");
				return;
			}

			printf("Hello from server id %d at %s\n", id, linkGetIpStr(link, SAFESTR(ip)));

			// Tell them what we've got.
			DSS_Advertise(link);
		}
		break;

		// A server is telling us what it has.
		case TO_DSS_ADVERTISE:
		{
			U32 parsetable_crc;
			int size;
			int i;
			int requests = 0;

			// Ignore advertisements from servers with different database schemas; we'll sync once our versions match.
			parsetable_crc = pktGetU32(pkt);
			if (parsetable_crc != (U32)ParseTableCRC(parse_DSNDatabase, NULL, 0))
				return;

			// Get database size.
			size = pktGetU32(pkt);

			printf("Server has %d machines; we have %d\n", size, beaSize(&sDatabase.machines));

			// Check machine IDs for any we're missing.
			for (i = 0; i != size; ++i)
			{
				U32 machine_id[8];
				UniqueMachine *machine;
				U32 struct_crc;

				// Get information for their struct.
				pktGetBytes(pkt, sizeof(machine_id), machine_id);
				machine = FindMachine(machine_id, false);
				struct_crc = pktGetU32(pkt);

				// If this isn't the same as our struct, ask for a copy of their struct.
				if (!machine || struct_crc != StructCRC(parse_UniqueMachine, machine))
				{
					reply = pktCreate(link, TO_DSS_REQUEST_UNIQUEMACHINE);
					pktSendBytes(reply, sizeof(machine_id), machine_id);
					pktSend(&reply);
					++requests;
					DSS_DebugSendRequest(machine_id, machine);
				}
			}
			printf("Requested %d machines\n", requests);
		}
		break;

		// A server is requesting information for a machine.
		case TO_DSS_REQUEST_UNIQUEMACHINE:
		{
			U32 machine_id[8];
			UniqueMachine *machine;

			// Look up the machine.
			pktGetBytes(pkt, sizeof(machine_id), machine_id);
			machine = FindMachine(machine_id, false);
			DSS_DebugReceiveRequest(machine_id, machine);
			if (!machine)
				return;

			// Send the machine.
			DSS_Send(link, machine);

			++sServerStatsMachinesSent;
		}
		break;

		// A server has sent us a copy of their machine, after a request for it or an update.
		case TO_DSS_UNIQUEMACHINE:
		{
			UniqueMachine machine = {0};
			ParserRecvStructSafe(parse_UniqueMachine, pkt, &machine);
			MergeMachine(&machine);
			StructDeInit(parse_UniqueMachine, &machine);

			++sServerStatsMachinesReceived;
		}
		break;
	}
}

// Another Deep Space Server connected
static int DSS_ServingConnect(NetLink *link, void *state)
{
	DsnClient *client = state;
	Packet *pkt;
	char ip[MAX_IP_STR];

	servLog(LOG_CLIENTSERVERCOMM, "DsnServerConnect", "ip %s", linkGetIpStr(link, SAFESTR(ip)));
	printf("Server %s connected\n", linkGetIpStr(link, SAFESTR(ip)));

	// Send Hello packet.
	pkt = pktCreate(link, TO_DSS_HELLO);
	pktSendU32(pkt, DEEPSPACE_SERVER_PROTOCOL);
	pktSendU32(pkt, sConfig.iServerId);
	pktSend(&pkt);

	// Add to server list.
	eaPush(&sConnectedServers, link);

	return 1;
}

// A Deep Space Server has disconnected.
static int DSS_ServingDisconnect(NetLink *link, void *state)
{
	DsnClient *client = state;
	char *reason = NULL;
	U32 error_code;
	char ip[MAX_IP_STR];

	// Remove from server list.
	eaFindAndRemove(&sConnectedServers, link);

	// Get disconnect reason.
	estrStackCreate(&reason);
	linkGetDisconnectReason(link, &reason);
	error_code = linkGetDisconnectErrorCode(link);

	// Log it.
	servLog(LOG_CLIENTSERVERCOMM, "DsnServerDisconnect", "ip %s reason \"%s\" code %d", linkGetIpStr(link, SAFESTR(ip)), reason, error_code);
	printf("Server %s disconnected: %s\n", linkGetIpStr(link, SAFESTR(ip)), reason);
	estrDestroy(&reason);

	return 1;
}

// Connect to all of the servers that we are supposed to connect to.
static void ConnectToServers()
{
	if (!eaSize(&sConfig.ppConnectToServer))
		return;

	if (!sConfig.ppConnectToServerFsm)
		eaSetSize(&sConfig.ppConnectToServerFsm, eaSize(&sConfig.ppConnectToServer));
	if (!sConfig.ppConnectToServerLink)
		eaSetSize(&sConfig.ppConnectToServerLink, eaSize(&sConfig.ppConnectToServer));
	EARRAY_CONST_FOREACH_BEGIN(sConfig.ppConnectToServer, i, n);
	{
		// Currently, use DEEPSPACE_PUBLIC_SERVER_PORT instead of DEEPSPACE_SERVER_PORT because of operational difficulties getting the private ports to span different networks.
		commConnectFSMForTickFunctionWithRetrying(&sConfig.ppConnectToServerFsm[i], &sConfig.ppConnectToServerLink[i], "DeepSpaceServer", 5, commDefault(),
			LINKTYPE_TOUNTRUSTED_100MEG, 0, sConfig.ppConnectToServer[i], DEEPSPACE_PUBLIC_SERVER_PORT, DSS_ServingMsg, DSS_ServingConnect, DSS_ServingDisconnect,
			0, NULL, NULL, NULL, NULL);
	}
	EARRAY_FOREACH_END;
}

// Get the filename of the database.
static const char *ConfigFilename()
{
	static char *filename = NULL;
	estrPrintf(&filename, "server/DeepSpaceServer/DeepSpaceServer.txt");
	return filename;
}

// Get the filename of the database.
static const char *DatabaseFilename()
{
	static char *filename = NULL;
	estrPrintf(&filename, "%s/server/DeepSpaceServer/DeepSpaceNetwork_Database.txt", fileLocalDataDir());
	return filename;
}

// Get a temporary file name.
static const char *DatabaseFilenameTemp()
{
	static char *filename = NULL;
	estrPrintf(&filename, "%s/server/DeepSpaceServer/DeepSpaceNetwork_Database.temp.txt", fileLocalDataDir());
	return filename;
}

// Compare machines by machine ID, for sorting.
static int CompareUniqueMachines(const void *lhs_ptr, const void *rhs_ptr)
{
	const UniqueMachine *lhs = lhs_ptr, *rhs = rhs_ptr;
	return memcmp(lhs->machine_id, rhs->machine_id, sizeof(lhs->machine_id));
}

// Compare events by time and id, for sorting.
static int CompareEvents(const void *lhs_ptr, const void *rhs_ptr)
{
	const BasicEvent *lhs = lhs_ptr, *rhs = rhs_ptr;

	// First, compare based on time.
	if (lhs->when < rhs->when)
		return -1;
	if (lhs->when > rhs->when)
		return 1;
	
	// Then, compare based on packet ID.
	if (lhs->packet_id < rhs->packet_id)
		return -1;
	if (lhs->packet_id > rhs->packet_id)
		return 1;

	// The same, for our purposes.
	return 0;
}

// Load the database from disk.
static void LoadDatabase()
{
	int i;
	bool old;
	extern bool gbFixedTokenizerBufferSize;

	PERFINFO_AUTO_START_FUNC();

	// Used fixed tokenizer buffer size, since the file might be really big.
	old = gbFixedTokenizerBufferSize;
	gbFixedTokenizerBufferSize = true;

	// Load text.
	loadstart_printf("Parsing database...");
	ParserReadTextFile(DatabaseFilename(), parse_DSNDatabase, &sDatabase, 0);
	loadend_printf("done.");

	// Sort machines by machine ID.
	loadstart_printf("Canonicalizing database...");
	qsort(sDatabase.machines, beaSize(&sDatabase.machines), sizeof(*sDatabase.machines), CompareUniqueMachines);

	// Sort events.
	for (i = 0; i != beaSize(&sDatabase.machines); ++i)
	{
		UniqueMachine *machine = sDatabase.machines + i;
		qsort(machine->started_events, beaSize(&machine->started_events), sizeof(*machine->started_events), CompareEvents);
		qsort(machine->exit_events, beaSize(&machine->exit_events), sizeof(*machine->exit_events), CompareEvents);
		qsort(machine->periodic_events, beaSize(&machine->periodic_events), sizeof(*machine->periodic_events), CompareEvents);
	}
	loadend_printf("done.");

	// Make sure the database is valid and consistent.
	loadstart_printf("Verifying database...");
	for (i = 0; i < beaSize(&sDatabase.machines) - 1; ++i)
	{
		UniqueMachine *machine = sDatabase.machines + i;
		int j;

		// Remove any duplicate machines.
		while (i < beaSize(&sDatabase.machines) - 1 && !memcmp(sDatabase.machines[i].machine_id, sDatabase.machines[i + 1].machine_id, sizeof(sDatabase.machines[i].machine_id)))
		{
			StructDeInit(parse_UniqueMachine, sDatabase.machines + i + 1);
			beaRemove(&sDatabase.machines, i + 1);
		}

		// Remove any duplicate events.
		// Also, set origin server, if it is not already set.
		// All events have the same concept, so use the following macro.
		// Obviously, this is a textbook case for a C++ template.  I decided that the following was better than
		// using a generic function using void * pointers, because this has better checking and better performance.
		// The downside is decreased debuggability, but hopefully it won't come to that.
#define REMOVE_DUPLICATE_EVENT_LOOP(FIELD)														\
		for (j = 0; j < beaSize(&machine->FIELD); ++j)											\
		{																						\
			if (!machine->FIELD[j].event.origin_server)											\
				machine->FIELD[j].event.origin_server = sConfig.iServerId;						\
			while (j < beaSize(&machine->FIELD) - 1												\
				&& machine->FIELD[j].event.packet_id == machine->FIELD[j + 1].event.packet_id)	\
				beaRemove(&machine->FIELD, j + 1);												\
		}

		// Remove duplication from event arrays.
		REMOVE_DUPLICATE_EVENT_LOOP(started_events);
		REMOVE_DUPLICATE_EVENT_LOOP(exit_events);
		REMOVE_DUPLICATE_EVENT_LOOP(periodic_events);
#undef REMOVE_DUPLICATE_EVENT_LOOP
	}
	loadend_printf("done.");

	gbFixedTokenizerBufferSize = old;

	PERFINFO_AUTO_STOP_FUNC();
}

// Background thread commands
enum SaveDatabaseThreadCmdMsg
{
	SaveDatabaseThreadCmd_Save = WT_CMD_USER_START,
};

// Save the database to disk, in a background thread.
static void SaveDatabaseThread(void *user_data, void *data, WTCmdPacket *packet)
{
	DSNDatabase *database = *(DSNDatabase **)data;
	int success;
	int result;

	PERFINFO_AUTO_START_FUNC();

	// Make directories.
	success = makeDirectoriesForFile(DatabaseFilenameTemp());
	if (!success)
		FatalErrorf("makeDirectoriesForFile %s", DatabaseFilenameTemp());
	success = makeDirectoriesForFile(DatabaseFilename());
	if (!success)
		FatalErrorf("makeDirectoriesForFile %s", DatabaseFilename());

	// Write out database.
	success = ParserWriteTextFile(DatabaseFilenameTemp(), parse_DSNDatabase, database, 0, 0);
	if (!success)
		FatalErrorf("ParserWriteTextFile %s", DatabaseFilenameTemp());

	// Move database into place.
	result = fileMove(DatabaseFilenameTemp(), DatabaseFilename());
	if (result)
		FatalErrorf("fileMove %s %s %d", DatabaseFilenameTemp(), DatabaseFilename(), success);

	// Free the database copy.
	StructDestroy(parse_DSNDatabase, database);

	PERFINFO_AUTO_STOP_FUNC();
}

// Save the database to disk.
static void SaveDatabase(bool wait)
{
	DSNDatabase *copy;

	PERFINFO_AUTO_START_FUNC();

	// Initialize background thread, if necessary.
	if (!save_thread)
	{
		save_thread = wtCreate(16, 16, NULL, "SaveDatabaseThread");
		wtRegisterCmdDispatch(save_thread, SaveDatabaseThreadCmd_Save, SaveDatabaseThread);
		wtSetThreaded(save_thread, true, 0, false);
		wtStart(save_thread);
	}

	// Copy the entire database: it is important that this step is as fast as possible.
	copy = StructClone(parse_DSNDatabase, &sDatabase);

	// Dispatch save request to background thread.
	wtQueueCmd(save_thread, SaveDatabaseThreadCmd_Save, &copy, sizeof(copy));

	// If wait requested, make sure it finishes.
	if (wait)
		wtFlush(save_thread);

	PERFINFO_AUTO_STOP_FUNC();
}

// Periodic tasks
void DsnPeriodic(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	SaveDatabase(false);
}

// Load the database from disk.
static void LoadConfig()
{
	int success;
	PERFINFO_AUTO_START_FUNC();

	// Load config file.
	success = ParserReadTextFile(ConfigFilename(), parse_DSNServerConfig, &sConfig, 0);
	if (!success)
		FatalErrorf("Unable to load config file %s", ConfigFilename());

	// Set server ID.
	if (!sConfig.iServerId)
		FatalErrorf("No server ID specified in config file");
	gServerLibState.containerID = sConfig.iServerId;

	PERFINFO_AUTO_STOP_FUNC();
}

// Report statistics periodically
void DsnPeriodicStats(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	if (sServerStatsMachinesReceived || sServerStatsMachinesSent)
	{
		printf("Server traffic: %d machines received, %d machines sent\n", sServerStatsMachinesReceived, sServerStatsMachinesSent);
		sServerStatsMachinesReceived = 0;
		sServerStatsMachinesSent = 0;
	}
}

// Set to true when it is time to close.
static bool gbCloseDeepSpaceServer = false;

// Shut down
static void consoleCloseHandler(DWORD fdwCtrlType)
{
	printf("Shutting down...\n");
	gbCloseDeepSpaceServer = true;
}

extern bool gbNeverConnectToController;

int wmain(int argc, WCHAR** argv_wide)
{
	int frameTimer, i, frame_count = 0;
	NetListen *listen;
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	WAIT_FOR_DEBUGGER

	setDefaultProductionMode(1);
	DO_AUTO_RUNS
	setDefaultAssertMode();
	gbNeverConnectToController = true;

	// Run stay up code instead of normal code if -StayUp was given
	if (StayUp(argc, argv, NULL, NULL, NULL, NULL))
		return 0;

	SetAppGlobalType(GLOBALTYPE_DEEPSPACESERVER);
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'D', 0x8080ff);

	FolderCacheChooseModeNoPigsInDevelopment();

	loadstart_printf("Initializing...");

	log_printf(LOG_MISC, "DeepSpaceServer Version: %s\n", GetUsefulVersionString());

	setConsoleTitle("DeepSpaceServer");

	LoadConfig();
	gServerLibState.iGenericHttpServingPort = DEFAULT_WEBMONITOR_DEEPSPACE;
	logSetDir(GlobalTypeToName(GetAppGlobalType()));
	logEnableHighPerformance();
	logAutoRotateLogFiles(true);
	serverLibStartup(argc, argv);

	setSafeCloseAction(consoleCloseHandler);
	useSafeCloseHandler();
	disableConsoleCloseButton();
	loadend_printf("done");

	// Load database.
	loadstart_printf("Loading database...");
	LoadDatabase();
	loadend_printf("done");

	// Listen on a bunch of ports.
	loadstart_printf("Opening ports...\n");
	listen = commListen(commDefault(), LINKTYPE_TOUNTRUSTED_100MEG, LINK_FORCE_FLUSH, DEEPSPACE_SERVER_PORT, DSS_ServingMsg, DSS_ServingConnect,
		DSS_ServingDisconnect, 0);
	if (!listen)
		FatalErrorf("Unable to listen on private DeepSpaceServer port!");
	listen = commListen(commDefault(), LINKTYPE_TOUNTRUSTED_100MEG, LINK_FORCE_FLUSH, DEEPSPACE_PUBLIC_SERVER_PORT, DSS_ServingMsg, DSS_ServingConnect,
		DSS_ServingDisconnect, 0);
	if (!listen)
		FatalErrorf("Unable to listen on public DeepSpaceServer port!");
	listen = commListen(commDefault(), LINKTYPE_TOUNTRUSTED_500K, LINK_RAW, sConfig.iOverridePort ? sConfig.iOverridePort : DEEPSPACE_PORT, DSN_ServingMsg, DSN_ServingConnect,
		DSN_ServingDisconnect, sizeof(DsnClient));
	if (!listen)
		FatalErrorf("Unable to listen on DSN client port!");
	EARRAY_INT_CONST_FOREACH_BEGIN(sConfig.pExtraPort, j, n);
	{
		listen = commListen(commDefault(), LINKTYPE_TOUNTRUSTED_500K, LINK_RAW, sConfig.pExtraPort[j], DSN_ServingMsg, DSN_ServingConnect,
			DSN_ServingDisconnect, sizeof(DsnClient));
		if (!listen)
			FatalErrorFilenamef(ConfigFilename(), "Unable to listen on extra port %d!", sConfig.pExtraPort[j]);
	}
	EARRAY_FOREACH_END;
	loadend_printf("done");

	frameTimer = timerAlloc();
	TimedCallback_Add(DsnPeriodic, NULL, 5.0f * 60.0f);
	TimedCallback_Add(DsnPeriodicStats, NULL, 60.0f);

	for(;;)
	{
		F32 frametime;
		
		autoTimerThreadFrameBegin(__FUNCTION__);
		
		frametime = timerElapsedAndStart(frameTimer);
		utilitiesLibOncePerFrame(frametime, 1);
		commMonitor(commDefault());
		serverLibOncePerFrame();

		// Connect to other servers, if necessary.
		ConnectToServers();

		// Process any outstanding reports.
		ProcessReports();

		// Exit if we've been asked to.
		if (gbCloseDeepSpaceServer)
			break;

		// After some frames have happened, declare that we are ready.
		++frame_count;
		if (frame_count == 1000)
			printf ("\nServer ready.\n\n");

		autoTimerThreadFrameEnd();
	}

	SaveDatabase(true);

	EXCEPTION_HANDLER_END
}

#include "DeepSpaceServer_c_ast.c"
#include "DeepSpaceServer_h_ast.c"
