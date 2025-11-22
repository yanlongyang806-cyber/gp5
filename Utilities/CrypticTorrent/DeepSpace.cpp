#include <process.h>
#include <string>
#include <windows.h>
#include <WinSock2.h>
#include <vector>
#include <Wincrypt.h>

#include "DeepSpace.hpp"
#include "UtilitiesLib/utils.h"

using std::vector;
using std::string;

// From DeepSpaceServer.c
enum DeepSpaceNetworkEvent {
	DeepSpaceNetworkEvent_CrypticTorrent_Started = 1,
	DeepSpaceNetworkEvent_CrypticTorrent_Exit,
	DeepSpaceNetworkEvent_CrypticTorrent_Periodic,
};

// A configured server, listed in deepspace_servers.
struct deepspace_server {
	const char *host;
	int port;
};

// List of servers we are configured to report to
deepspace_server deepspace_servers[] = {
	{"lg.deepspace.crypticstudios.com", 80},
	{"bos.deepspace.crypticstudios.com", 80},
	{"lg.deepspace.crypticstudios.com", 443},
	{"lg.deepspace.crypticstudios.com", 7454},
};

// Resolved servers
struct resolved_deepspace_server {
	struct in_addr addr;
	int port;
};

// Servers from deepspace_servers that we were able to resolve
vector<resolved_deepspace_server> resolved_servers;

// Locks creation of resolved_servers (not access)
CRITICAL_SECTION resolved_servers_mutex;

// Our product name
string deepspace_product;

// The torrent info hash
char deepspace_info_hash[32];

// Set our basic reporting information.
void DeepSpaceInitInfo(const char *product, const char *info_hash32)
{
	deepspace_product = product;
	memcpy(deepspace_info_hash, info_hash32, sizeof(deepspace_info_hash));
}

// Get all of the IP addresses for a host.
struct in_addr **resolve(const char *host)
{
	struct hostent *ent = gethostbyname(host);
	if (!ent)
		return NULL;
	return (struct in_addr **)ent->h_addr_list;
}

// Information to send to the Deep Space Network.
struct ReportInfoStruct
{
	string packet;
};

// Transmit in a background thread.
void BackgroundReportInfo(void *data)
{

	ReportInfoStruct *info = (ReportInfoStruct *)data;

	// Resolve server list.
	EnterCriticalSection(&resolved_servers_mutex);
	if (resolved_servers.empty())
	{
		for (int i = 0; i != sizeof(deepspace_servers)/sizeof(*deepspace_servers); ++i)
		{
			deepspace_server &server = deepspace_servers[i];
			struct in_addr **addr_list = resolve(server.host);
			if (!addr_list)
				continue;
			for (int j = 0; addr_list[j]; ++j)
			{
				resolved_deepspace_server resolved_server;
				resolved_server.addr = *addr_list[j];
				resolved_server.port = htons(server.port);
				resolved_servers.push_back(resolved_server);
			}
		}
	}
	LeaveCriticalSection(&resolved_servers_mutex);

	// Try to send message to each server until one of them gets it.
	for (int i = 0; i != resolved_servers.size(); ++i)
	{
		DWORD timeout = 15000;
		resolved_deepspace_server &server = resolved_servers[i];

		// Create a socket.
		int sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock == SOCKET_ERROR)
		{
			delete info;
			return;
		}

		// Set options.
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
		setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));

		// Try to connect.
		sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_addr = server.addr;
		addr.sin_port = server.port;
		int result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
		if (result == SOCKET_ERROR)
		{
			closesocket(sock);
			continue;
		}

		// Try to send message.
		result = send(sock, info->packet.c_str(), info->packet.length(), 0);
		if (result == SOCKET_ERROR)
		{
			closesocket(sock);
			continue;
		}

		// Wait for confirmation.
		char c;
		result = recv(sock, &c, 1, 0);
		if (result == SOCKET_ERROR || !result || c != '.')
		{
			closesocket(sock);
			continue;
		}

		// Close socket.
		closesocket(sock);
		delete info;
		return;
	}
}

// Create a unique packet id.
void make_packet_id(char *id, size_t id_size)
{
	HCRYPTPROV hCryptProv;
	BOOL success;

	// Acquire context.
	success = CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, 0);
	if (!success)
		return;

	// Generate random buffer.
	success = CryptGenRandom(hCryptProv, id_size, (BYTE *)id);

	// Release context.
	CryptReleaseContext(hCryptProv, 0);
}

// Create a packet.
string create_packet(int event, char *str, size_t str_len)
{
	string packet;
	char machine_id[32];

	// packet description:
	// bytes[4]:	protocol magic "DSN\0"
	// bytes[4]:	packet size
	// bytes[8]:	packet id
	// bytes[4]:	what sort of thing we are
	// bytes[32]:	machine ID
	// bytes[32]:	torrent info hash (or similar descriptor)
	// bytes[4]:	Windows default LCID
	// bytes[4]:	CrypticTorrent version
	// bytes[4]:	Event ID
	// SZ:			product name
	// bytes:		payload, depending on event, sometimes SZ, sometimes blob

	char id[8];
	make_packet_id(id, sizeof(id));
	packet.append(id, sizeof(id));
	packet.append("CDT");  // Cryptic Downloader Torrent
	packet.push_back(0);
	getMachineID(machine_id);
	packet.append(machine_id, 32);
	packet.append(deepspace_info_hash, 32);
	LCID lcid = GetUserDefaultLCID();
	packet.append((char *)&lcid, 4);
	int version = 1;
	packet.append((char *)&version, 4);
	packet.append((char *)&event, 4);
	
	packet.append(deepspace_product);
	packet.push_back(0);

	packet.append(str, str_len);
	packet.push_back(0);

	int size = packet.length() + 8;
	packet.insert(0, (char *)&size, 4);
	packet.insert(0, "DSN", 4);

	return packet;
}

// Send an event to the Deep Space Network.
void DeepSpaceSend(bool synchronous, int event, char *string, size_t str_len)
{

	InitializeCriticalSection(&resolved_servers_mutex);

	// Initialize networking.
	static bool init_done = false;
	if (!init_done)
	{
		WSADATA wsaData;
		int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (result)
			return;
		init_done = true;
	}

	// Create a packet, and send it to a background thread to be sent.
	struct ReportInfoStruct *info = new ReportInfoStruct;
	info->packet = create_packet(event, string, str_len);

	// Send to thread (or run in foreground, if synchronous)
	if (synchronous)
		BackgroundReportInfo((void*)info);
	else
		_beginthread(BackgroundReportInfo, 0, info);
}

// Report that we've started up.
void DeepSpaceReportStartup()
{
	DeepSpaceSend(false, DeepSpaceNetworkEvent_CrypticTorrent_Started, "", 0);
}

// Report an exit.
void DeepSpaceSyncReportExit(bool started_download, bool got_bytes, int download_percent, bool download_finished, bool installer_ran)
{
	char buf[100];
	sprintf(buf, "%d %d %d %d %d", (int)started_download, (int)got_bytes, download_percent, (int)download_finished, (int)installer_ran);
	DeepSpaceSend(true, DeepSpaceNetworkEvent_CrypticTorrent_Exit, buf, strlen(buf));
}

// Periodic download status.
void DeepSpaceSyncReportPeriodicDownload(bool final, const struct DeepSpacePeriodicTorrentBlob &blob)
{
	char buf[300];
	*(int *)buf = sizeof(blob);
	*(int *)(buf+4) = (int)final;
	memcpy(buf + 8, &blob, sizeof(blob));
	DeepSpaceSend(false, DeepSpaceNetworkEvent_CrypticTorrent_Periodic, buf, sizeof(blob) + 8);
}
