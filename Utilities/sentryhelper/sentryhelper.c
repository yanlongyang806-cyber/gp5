#include "sysutil.h"
#include "cmdparse.h"
#include "gimmeDLLWrapper.h"
#include "zutils.h"
#include "net.h"
#include "structNet.h"
#include "file.h"
#include "utilitiesLib.h"


#include "../SentryServer/sentrypub.h"
#include "../SentryServer/sentry_comm.h"

#include "autogen/sentrypub_h_ast.h"

char *g_server = "sentryserver";
AUTO_COMMAND ACMD_CMDLINE; void server(char *server) { g_server = strdup(server); }

bool g_done = false;
bool g_failed = false;
SentryClientList g_clients = {0};
void (*g_clientcb)(SentryClientList *) = NULL;
void SentryServerMessageCB(Packet *pak,int cmd, NetLink *link, void *pUserData)
{
	char *pResult;

	switch (cmd)
	{

		xcase MONITORSERVER_EXPRESSIONQUERY_RESULT:
			StructDeInit(parse_SentryClientList, &g_clients);
			ParserRecv(parse_SentryClientList, pak, &g_clients, 0);
			if(g_clientcb)
				g_clientcb(&g_clients);
			g_done = true;
		xcase MONITORSERVER_MSG:
			pResult = pktGetStringTemp(pak);
			printf("%s\n", pResult);
			g_done = true;

			//possibilities are "ok", notFound", "notConnected"
			if (strstri(pResult, "not"))
			{
				g_failed = true;
			}
	}
}

NetLink *g_link = NULL;
NetLink *getLink(void)
{
	if(!g_link)
		g_link = commConnectWait(commDefault(), LINKTYPE_DEFAULT, LINK_FORCE_FLUSH, g_server, SENTRYSERVERMONITOR_PORT, SentryServerMessageCB, NULL, NULL, 0, 0);
	if(!linkConnected(g_link) || linkDisconnected(g_link))
		assert(0);
	return g_link;
}

AUTO_COMMAND ACMD_CMDLINE;
void LaunchWithWorkingDir(char *pMachineName, char *pCommand, char *pDir)
{
	char *pFullCmd = NULL;
	Packet *pak = pktCreate(getLink(), MONITORCLIENT_LAUNCH);
	estrPrintf(&pFullCmd, "WORKINGDIR(%s) %s", pDir, pCommand);
	estrReplaceOccurrences(&pFullCmd, "\\q", "\"");
	pktSendString(pak, pMachineName);
	pktSendString(pak, pFullCmd);
	pktSend(&pak);
	estrDestroy(&pFullCmd);
}



AUTO_COMMAND ACMD_CMDLINE;
void sendFile(char *pMachineName, char *pLocalFile, char *pRemoteFile)
{
	int iInFileSize;
	char *pInBuffer = fileAlloc(pLocalFile, &iInFileSize);

	char *pCompressedBuffer;
	int iCompressedSize;
	Packet *pPak = pktCreate(getLink(), MONITORCLIENT_CREATEFILE);

	assertmsgf(pInBuffer, "Couldn't read file %s in order to send it via sentry server", pLocalFile);

	pCompressedBuffer = zipData(pInBuffer, iInFileSize, &iCompressedSize);

	pktSendString(pPak, pMachineName);
	pktSendString(pPak, pRemoteFile);
	pktSendBits(pPak, 32, iCompressedSize);
	pktSendBits(pPak, 32, iInFileSize);
	pktSendBytes(pPak, iCompressedSize, pCompressedBuffer);

	pktSend(&pPak);

	free(pInBuffer);
	free(pCompressedBuffer);
}

// NOTE: Can't use kill() as a function name since it conflicts with a built-in. <NPK 2009-4-23>
AUTO_COMMAND ACMD_CMDLINE ACMD_NAME(kill);
void kill_cmd(char *pMachineName, char *pExeName)
{
	Packet *pPak;

	pPak = pktCreate(getLink(), MONITORCLIENT_KILL);
	pktSendString(pPak, pMachineName);
	pktSendString(pPak, pExeName);
	pktSend(&pPak);
}

AUTO_COMMAND ACMD_CMDLINE;
void KillAllInDir(char *pMachineName, char *pExeName, char *pDir)
{
	char *pFullCmd = NULL;
	Packet *pak = pktCreate(getLink(), MONITORCLIENT_LAUNCH_AND_WAIT);

	estrPrintf(&pFullCmd, "cryptickillall");

	estrConcatf(&pFullCmd, " -kill %s", pExeName);
	
	estrConcatf(&pFullCmd, " -restrictToDir %s", pDir);

	pktSendString(pak, pMachineName);
	pktSendString(pak, pFullCmd);
	pktSend(&pak);
	estrDestroy(&pFullCmd);
}


AUTO_COMMAND ACMD_CMDLINE;
void launch(char *machine, char *cmd)
{
	char *pCmdCopy = NULL;
	Packet *pak = pktCreate(getLink(), MONITORCLIENT_LAUNCH);
	pktSendString(pak, machine);

	estrCopy2(&pCmdCopy, cmd);
	estrReplaceOccurrences(&pCmdCopy, "\\q", "\"");
	pktSendString(pak, pCmdCopy);
	pktSend(&pak);
	estrDestroy(&pCmdCopy);
}

void showList(SentryClientList *clients)
{
	FOR_EACH_IN_EARRAY(clients->ppClients, SentryClient, client)
		printf("%s\n", client->name);
	FOR_EACH_END
}

AUTO_COMMAND ACMD_CMDLINE ACMD_NAME(list);
void listClients(int dummy)
{
	Packet *pPack = pktCreate(getLink(), MONITORCLIENT_EXPRESSIONQUERY);
	pktSendBits(pPack, 32, 0); //flags of type EXPRESSIONQUERY_FLAG_
	pktSendString(pPack, "1");
	pktSend(&pPack);
	g_clientcb = showList;
}

extern bool gbPrintCommandLine;

int main(int argc, char **argv)
{
	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS

	gbCavemanMode = 1;
	
	gbPrintCommandLine = false;
	gimmeDLLDisable(true);
	if(argc == 1)
	{
		printf("sentryhelper: A tool to script sentry operations\n"
			   "  -list : Print a list of all known machine names\n"
			   "  -launch <machine> <cmd> : Run a command on a remote machine\n"
			   "  -kill <machine> <executable> : Kill all instances of a given executable\n"
			   "  -sendFile <machine> <local file> <remote file> : Copy a file to a remote machine\n"
			   "  -KillAllInDir <machine> <executable> <dirname> : like kill, but only kills exe launched from a given root dir\n"
			   "  -LaunchWithWorkingDir <machine> <cmd> <dir> : launches the specified command in a working dir\n"
			   "Options:\n"
			   "  -server <name> : Sentry server to use (default: \"sentryserver\")\n");
		return 0;
	}
	cmdParseCommandLine(argc, argv);
	while(!g_done)
		commMonitor(commDefault());
	commFlushAndCloseAllLinks(commDefault());
	
	if (g_failed)
	{
		exit(-1);
	}

	EXCEPTION_HANDLER_END

	return 0;
}
