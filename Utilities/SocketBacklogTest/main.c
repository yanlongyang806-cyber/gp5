#include "cmdparse.h"
#include "gimmeDLLWrapper.h"
#include "net/net.h"
#include "sock.h"
#include "sysutil.h"
#include "utilitiesLib.h"
#include "windefinclude.h"

#define SOCKBACKLOG_TEST_PORT 7171

// Listen instead of connecting out
int listenMode = 0;
AUTO_CMD_INT(listenMode, listenMode) ACMD_CMDLINE;

// Override the backlog size; 0 = SOMAXCONN; 1 => 2; 2 => 20
int backlogSize = 0;
AUTO_CMD_INT(backlogSize, backlogSize) ACMD_CMDLINE;

// Host to connect to, if not in listen mode
char targetHost[64] = "localhost";
AUTO_CMD_STRING(targetHost, targetHost) ACMD_CMDLINE;

// Port to use for listening/connecting
int port = SOCKBACKLOG_TEST_PORT;
AUTO_CMD_INT(port, port) ACMD_CMDLINE;

// Delay between initiating new links
int linkDelay = 0;
AUTO_CMD_INT(linkDelay, linkDelay) ACMD_CMDLINE;

// Maximum number of links to attempt
int maxLinks = 0;
AUTO_CMD_INT(maxLinks, maxLinks) ACMD_CMDLINE;

static int accepted = 0;

void connect_cb(NetLink *link, void *user_data)
{
	++accepted;
	linkRemove_wReason(&link, "accepted");
}

int main(int argc, char **argv)
{
	EXCEPTION_HANDLER_BEGIN;
	WAIT_FOR_DEBUGGER;
	DO_AUTO_RUNS;

	gimmeDLLDisable(1);
	cmdParseCommandLine(argc, argv);
	utilitiesLibStartup();

	if (listenMode)
	{
		NetListen *local, *public;
		int acceptedPrivate = 0, acceptedPublic = 0;
		U32 flags = LINK_NO_COMPRESS;

		if (backlogSize == 1)
			flags |= LINK_SMALL_LISTEN;
		else if (backlogSize == 2)
			flags |= LINK_MEDIUM_LISTEN;

		// Open socket
		commListenBoth(commDefault(), LINKTYPE_DEFAULT, flags, port, NULL, connect_cb, NULL, 0, &local, &public);

		// Loop forever until key pressed
		printf("Press a key to begin accepting...\n");
		while (1)
		{
			Sleep(1);
			if (_kbhit())
				break;
		}

		commMonitor(commDefault());
		printf("Accepted %d connections\n", accepted);
	}
	else
	{
		S64 lastLinkTime = 0;
		int links = 0;
		int printOnce = 0;

		printf("Press a key to stop connecting.\n");

		do
		{
			S64 now = timeMsecsSince2000();

			if ((!maxLinks || links < maxLinks) && now - lastLinkTime >= linkDelay)
			{
				commConnect(commDefault(), LINKTYPE_DEFAULT, LINK_NO_COMPRESS, targetHost, port, NULL, connect_cb, NULL, 0);
				lastLinkTime = now;
				++links;
			}
			else if (!printOnce && maxLinks && links >= maxLinks)
			{
				printOnce = 1;
				printf("All connections initiated.\n");
			}

			commMonitor(commDefault());
		} while (!_kbhit());
	}

	printf("Press a key to exit.\n");
	while (1)
	{
		Sleep(1);
		if (_kbhit())
			break;
	}

	EXCEPTION_HANDLER_END;
	return 0;
}