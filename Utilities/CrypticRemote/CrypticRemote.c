#include "netio.h"
#include "net_packet.h"
#include "net_masterlist.h"
#include "net_link.h"
#include "sock.h"
#include "memcheck.h"
#include "error.h"
#include <conio.h>
#include <process.h>
#include "timing.h"
#include "mathutil.h"
#include "netio_stats.h"
#include "utils.h"
#include "sysutil.h"
#include "ClipboardMonitor.h"
#include "file.h"
#include <ShlObj.h>
#include "process_util.h"


#define TCP_PORT 4731

enum {
	NETCMD_STARTPROCESS = COMM_MAX_CMD,
};


typedef struct DummyClientLink {
	NetLink *link;
	int dummy;
} DummyClientLink;

int connectCallback(NetLink *link)
{
	DummyClientLink	*client;
	client = link->userData;
	client->link = link;
	printf("New connect from %s\n", makeIpStr(link->addr.sin_addr.S_un.S_addr));
	return 1;
}

int delCallback(NetLink *link)
{
	DummyClientLink	*client = link->userData;
	printf("Disconnect from %s\n", makeIpStr(link->addr.sin_addr.S_un.S_addr));
	return 1;
}


int handleMsg(Packet *pak,int cmd, NetLink *link)
{
	// Handles either side's messages
	switch (cmd) {
		case NETCMD_STARTPROCESS:
			{
				char *s = pktGetString(pak);
				int minimized = pktGetBits(pak, 1);
				printf("Launching \"%s\"...\n", s);
				system_detach(s, minimized);
			}
			break;
		default:
			printf("Unknown command %d!\n", cmd);
			break;
	}
	return 1;
}

void crypticRemoteInstall(void)
{
	char startMenu[MAX_PATH];
	SHGetSpecialFolderPath(compatibleGetConsoleWindow(), startMenu, CSIDL_COMMON_STARTUP , FALSE);
	strcat(startMenu, "\\CrypticRemote.lnk");
	if (fileExists(startMenu)) {
		// Remove the old one
		fileForceRemove(startMenu);
	}
	createShortcut(getExecutableName(), startMenu, 0, NULL, "/hide /listen", NULL );

	// Start a new one running
	if (1==ProcessCount("CrypticRemote.exe")) {
		char buf[MAX_PATH];
		sprintf(buf, "%s /hide /listen", getExecutableName());
		system_detach(buf, 1);
	}
}

void showhelp()
{
	printf("CrypticRemote Control program\n");
	printf("Server usage:\n");
	printf("  CrypticRemote [/hide] /listen\n");
	printf("Client usage:\n");
	printf("  CrypticRemote [/min] [/notimeout] hostname command\n");
	printf("Clipboard file transfer:\n");
	printf("  Server: start as above (\"CrypticRemote /hide /listen\")\n");
	printf("  Client: CrypticRemote /send <filename>\n");
}

int main(int argc, char *argv[])
{
	int i;
	enum {
		MODE_CLIENT,
		MODE_SERVER,
		MODE_CLIPBOARD_SEND,
	} mode=MODE_CLIENT;
	bool hide=false;
	bool hidden=false;
	int minimized=0;
	int client_command_start=1;
	F32 timeout=15.f;
	//memCheckInit();

	setConsoleTitle("CrypticRemote");

	for (i=1; i<argc; i++)
	{
		char *param;
		if (argv[i][0]=='-' || argv[i][0]=='/')
		{
			param = argv[i];
			while (*param=='-' || *param=='/') param++;
#define CMD(s) (stricmp(param, s)==0)
            if (CMD("?") || CMD("h") || CMD("help"))
			{
				showhelp();
				return 0;
			}
			if (CMD("hide")) {
				hide = true;
			}
			if (CMD("listen")) {
				mode = MODE_SERVER;
			}
			if (CMD("min")) {
				minimized = 1;
				client_command_start = i+1;
			}
			if (CMD("notimeout")) {
				client_command_start = i+1;
				timeout = 100000000.f;
			}
			if (CMD("send")) {
				client_command_start = i+1;
				mode = MODE_CLIPBOARD_SEND;
			}
			if (CMD("install")) {
				crypticRemoteInstall();
				return 0;
			}
		}
	}

	if (hide && mode==MODE_SERVER) {
		hidden = true;
		hideConsoleWindow();
	}

	fileAllPathsAbsolute(true);
	sockStart();
	packetStartup(0,0); // TODO: enable encryption

	if (mode == MODE_SERVER) {
		NetLinkList *links = calloc(sizeof(NetLinkList), 1);
		netLinkListAlloc(links,16,sizeof(DummyClientLink),connectCallback);
		if (netInit(links,0,TCP_PORT)) {
			links->destroyCallback = delCallback;
			NMAddLinkList(links, handleMsg);
			printf("Now listening on TCP port %d\n", TCP_PORT);
			while (true) {
				NMMonitor(1);
				clipboardMonitor();
				if (!clipboardMonitorActive() && hide && !hidden) {
					hideConsoleWindow();
					hidden = true;
				} else if (clipboardMonitorActive() && hidden) {
					showConsoleWindow();
					hidden = false;
				}
			}
			return 0;
		} else {
			printf("Failed to start listening\n");
			return 2;
		}
		return 0;
	}
	if (mode == MODE_CLIENT || mode == MODE_CLIPBOARD_SEND) {
		NetLink *link = createNetLink();
		char command[2048]="";
		if (mode == MODE_CLIENT && (argc - client_command_start < 2) ||
			mode == MODE_CLIPBOARD_SEND && (argc - client_command_start < 1))
		{
			showhelp();
			return 1;
		}
		for (i=client_command_start+((mode == MODE_CLIENT)?1:0); i<argc; i++) {
			strcat(command, argv[i]);
			if (i!=argc-1)
				strcat(command, " ");
		}
		if (mode == MODE_CLIPBOARD_SEND) {
			clipboardSend(command);
		} else {
			printf("Connecting to %s:%d... ", argv[client_command_start], TCP_PORT);
			if (netConnect(link, argv[client_command_start], TCP_PORT, NLT_TCP, timeout, NULL)) {
				Packet *pak = pktCreateEx(link, NETCMD_STARTPROCESS);
				pktSendString(pak, command);
				pktSendBits(pak, 1, minimized);
				pktSend(&pak, link);
				lnkFlushAll();
				printf("done.\n");
			} else {
				printf("Failed to connect\n");
				return 3;
			}
		}
	}
	return 0;
}