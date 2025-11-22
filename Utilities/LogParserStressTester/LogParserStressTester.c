#include "MemoryMonitor.h"
#include "FolderCache.h"
#include "sysUtil.h"
#include "UtilitiesLib.h"
#include "cmdParse.h"
#include "file.h"
#include "Estring.h"
#include "earray.h"
#include "StringCache.h"
#include "process_Util.h"
#include "net.h"
#include "../../utilities/sentryserver/sentry_comm.h"
#include "timing.h"
#include "LogParsingFileBuckets.h"

#include "fileutil2.h"
#include "loggingEnums.h"
#include "CrypticPorts.h"
#include "serverLib.h"
#include "logging.h"
#include "LogParserStressTester_c_ast.h"


static char *spDirToLoadLogs = NULL;
AUTO_CMD_ESTRING(spDirToLoadLogs, DirToLoadLogs);

static U32 siStartingTime = 0;
AUTO_CMD_INT(siStartingTime, StartingTime);

static int siMegsOfLogsToLoad = 1000;
AUTO_CMD_INT(siMegsOfLogsToLoad, MegsOfLogsToLoad);

AUTO_STRUCT;
typedef struct LoadedLog
{
	const char *pFileNameKey; AST(POOL_STRING)
	char *pLogString; 
} LoadedLog;

LoadedLog **ppLoadedLogs = NULL;

static U64 siSizeOfLoadedLogs = 0;

void LoadLogs(void)
{
	FileNameBucketList *pBucketList = NULL;
	char **ppFiles = NULL;
	LogParsingRestrictions restrictions = {0};
	int iCounter = 0;

	if (siStartingTime)
	{
		restrictions.iStartingTime = siStartingTime;
		restrictions.iEndingTime = timeSecondsSince2000();
	}

	assertmsgf(spDirToLoadLogs, "Must specify -DirToLoadLogs");

	ppFiles = fileScanDir(spDirToLoadLogs);

	pBucketList = CreateFileNameBucketList(NULL, NULL, 0);
	DivideFileListIntoBuckets(pBucketList, &ppFiles);
	ApplyRestrictionsToBucketList(pBucketList, &restrictions);

	printf("About to start loading logs into RAM\n");

	while (siSizeOfLoadedLogs < ((U64)siMegsOfLogsToLoad) * 1024 * 1024)
	{
		char *pString, *pFileName;
		LoadedLog *pLoadedLog;
		U32 iTime, iID;

		if (!GetNextLogStringFromBucketList(pBucketList, &pString /*NOT AN ESTRING*/, &pFileName,
			&iTime, &iID))
		{
			printf("No more logs in the bucket!\n");
			return;
		}

		pLoadedLog = StructCreate(parse_LoadedLog);
		pLoadedLog->pLogString = strdup(pString);
		pLoadedLog->pFileNameKey = GetKeyFromLogFilename(pFileName);
		eaPush(&ppLoadedLogs, pLoadedLog);

		siSizeOfLoadedLogs += strlen(pString);

		iCounter++;

		if (iCounter % 1000 == 0)
		{
			static char *spTempSizeString = NULL;

			estrMakePrettyBytesString(&spTempSizeString, siSizeOfLoadedLogs);
			printf("Loaded %d logs... %s\n", iCounter, spTempSizeString);
		}
	}

	{
		int iBrk = 0;
	}
}

static char sLogParserName[256] = "localhost";
AUTO_CMD_STRING(sLogParserName, LogParserName);

NetListen *logServerForLogParserLinks;
NetLink *pLinkToLogParser;

static bool sbConnected = false;

void StressTester_ResizeCB(int iNewSize)
{
	printf("Resized to %u\n", iNewSize);
}

void StressTester_SleepCB(void)
{
	printf("Slept\n");
}

void StressTester_HandleConnect(NetLink *link,void *pUserData)
{
	printf("Got connection from log parser\n");
	sbConnected = true;
	pLinkToLogParser = link;

	linkSetSleepCB(link, StressTester_SleepCB);
	linkSetResizeCB(link, StressTester_ResizeCB);
	
}

void StressTester_HandleDisconnect(NetLink *link,void *pUserData)
{

	assertmsgf(0, "Log parser disconnected... something has failed");

}

void ConnectToLogParser(void)
{
	printf("Opening listening link for logParser\n");

	for (;;)
	{
		logServerForLogParserLinks = commListen(commDefault(),LINKTYPE_SHARD_CRITICAL_500K, LINK_FORCE_FLUSH, LOGSERVER_LOGPARSER_PORT, NULL, StressTester_HandleConnect,
			StressTester_HandleDisconnect, 0);
		if (logServerForLogParserLinks)
			break;
		Sleep(1);
	}
	
	printf("Waiting for log parser\n");

	while (!sbConnected)
	{
		commMonitor(commDefault());
		Sleep(1);
	}

}

#define LOGS_PER_PACKET 10

void SendAllLogs(void)
{
	int iLogsToSend = eaSize(&ppLoadedLogs);
	int i;
	Packet *pPacket = NULL;
	int iLogsThisPacket;

	printf("About to send %d logs\n", iLogsToSend);

	for (i = 0; i < iLogsToSend; i++)
	{
		if (!pPacket)
		{
			pPacket = pktCreate(pLinkToLogParser, LOGSERVER_TO_LOGPARSER_HERE_ARE_LOGS);
			iLogsThisPacket = 0;
		}

		pktSendString(pPacket, ppLoadedLogs[i]->pFileNameKey);
		pktSendString(pPacket, ppLoadedLogs[i]->pLogString);

		iLogsThisPacket++;

		if (iLogsThisPacket == LOGS_PER_PACKET)
		{
			pktSendString(pPacket, "");
			pktSend(&pPacket);
		}

		if (i % 100000 == 0)
		{
			printf("Sent %d logs\n", i);
		}
	}

	if (pPacket)
	{
		pktSendString(pPacket, "");
		pktSend(&pPacket);
	}
}



int main(int argc,char **argv)
{
	int i;


	EXCEPTION_HANDLER_BEGIN
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	FolderCacheChooseMode();


	preloadDLLs(0);


	utilitiesLibStartup();


	cmdParseCommandLine(argc, argv);



	srand((unsigned int)time(NULL));

	fileAllPathsAbsolute(true);

	LoadLogs();
	ConnectToLogParser();
	SendAllLogs();


	EXCEPTION_HANDLER_END

}


#include "LogParserStressTester_c_ast.c"
