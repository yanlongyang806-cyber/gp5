#include "sock.h"
#include "net.h"
#include "timing.h"
#include "earray.h"
#include "estring.h"
#include "file.h"
#include "utils.h"
#include "sentrycomm.h"
#include "webcomm.h"
#include "error.h"
#include "logging.h"
#include "fileutil.h"
#include "sentrycfg.h"
#include "timing.h"
#include "monitor.h"
#include "cmdparse.h"
#include "sentryalerts.h"
#include "MemoryMonitor.h"
#include "MemReport.h"
#include "utilitiesLib.h"
#include "textparser.h"
#include "sentrypub_h_ast.h"
#include "FolderCache.h"
#include "TimedCallback.h"
#include "sysutil.h"


#define	PHYSX_SRC_FOLDER "../../3rdparty"
#include "../../libs/worldlib/pub/PhysicsSDK.h"
NetComm	*comm;
SentryServerMachineList *machine_list;


char	sentry_name[256];
int		alert_delay = 30;
int		no_web = 0;
int		web_port = 80;

AUTO_CMD_STRING(sentry_name, sentry);
AUTO_CMD_INT(alert_delay,alert_delay);
AUTO_CMD_INT(no_web,no_web);
AUTO_CMD_INT(web_port,web_port);



#define READONLYSERVERS_FILENAME "ReadOnlyMachines.txt"
#define ALERTONDISCONNECTSERVERS_FILENAME "AlertOnDisconnectMachines.txt"

static void reloadLocalMachineLists(const char *relpath, int when);

static char **sppReadOnlyServers = NULL;
static char **sppAlertOnDisconnectServers = NULL;

bool clientIsServerSideReadOnly(const char *pClientName)
{
	if (eaFindString(&sppReadOnlyServers, pClientName) != -1)
	{
		return true;
	}

	return false;
}

bool clientIsAlertOnDisconnect(const char *pClientName)
{
	if (eaFindString(&sppAlertOnDisconnectServers, pClientName) != -1)
	{
		return true;
	}

	return false;
}

void LoadMachineList(char ***pppOutList, const char *pFileName, char *pListDesc)
{
	char *pBuf;

	eaDestroyEx(pppOutList, NULL);
	pBuf = fileAlloc(pFileName, NULL);
	if (pBuf)
	{
		int i;

		DivideString(pBuf, "\n\r", pppOutList, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE 
			| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);
		free(pBuf);

		if (eaSize(pppOutList) == 0)
		{
			printf("Read %s, it was empty, so no %ss\n", pFileName, pListDesc);
		}
		else
		{
			printf("Loaded %s, found %d machine%s to make %s\n",
				pFileName, eaSize(pppOutList), 
				 eaSize(pppOutList) == 1 ? "" : "s", pListDesc);

			for (i = 0; i < eaSize(pppOutList); i++)
			{
				printf("%s\n", (*pppOutList)[i]);
			}
		}
	}
	else
	{
		printf("Couldn't load anything from %s... currently no %ss\n", 
			pFileName, pListDesc);
	}
}

void LoadLocalMachineLists(void)
{

	ONCE(FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "*.txt", reloadLocalMachineLists));

	LoadMachineList(&sppReadOnlyServers, READONLYSERVERS_FILENAME, "server-side read-only machine");
	LoadMachineList(&sppAlertOnDisconnectServers, ALERTONDISCONNECTSERVERS_FILENAME, "alert-on-disconnect machine");
}
	

static void reloadLocalMachineLists(const char *relpath, int when)
{
	LoadLocalMachineLists();
}


static void LogFlushCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	logCloseAllLogs();
}

int wmain(int argc, WCHAR** argv_wide)
{
	int		timer = timerAlloc(), alert_timer = timerAlloc();
	char **argv;

	gbCavemanMode = 1;
	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	DO_AUTO_RUNS

//	fileAllPathsAbsolute(1);
	FolderCacheChooseModeNoPigsInDevelopment();

	FolderCacheEnableCallbacks(1);
	FolderCacheSetManualCallbackMode(1);
	FolderCacheStartMonitorThread();

	utilitiesLibStartup();


	cmdParseCommandLine(argc,argv);

	gbZipAllLogs = true;
	logEnableHighPerformance();
	TimedCallback_Add(LogFlushCB, NULL, 15.0f);

	filelog_printf("info.log","SentryServer starting");
	{
		char	curr_dir[MAX_PATH],fname[MAX_PATH];

		fileGetcwd(curr_dir,sizeof(curr_dir));
		sprintf(fname,"%s/%s",curr_dir,"SentryCfg.txt");
		machine_list = StructCreate(parse_SentryServerMachineList);
		if (!ParserReadTextFile(fname, parse_SentryServerMachineList, machine_list, 0))
			FatalErrorf("failed to parse %s!",fname);
	}
	alertLoad();

	printf("waiting for sentries..\n");
	comm = commCreate(20,0);
	if (!no_web)
		webListen(comm,web_port);
	sentryListen(comm,sentry_name[0] ? sentry_name : 0);
	monitorListen(comm);

	LoadLocalMachineLists();

	for(;;)
	{
		commMonitor(comm);
		commMonitor(commDefault());
		utilitiesLibOncePerFrame(REAL_TIME);

		if (timerElapsed(timer) > 1.f)
		{
			
			timerStart(timer);
			sentryUpdate();
		}
		if (timerElapsed(alert_timer) > alert_delay) // give sentries some time to connect
		{
			alertCheck();
		}

		FolderCacheDoCallbacks();
	}
	EXCEPTION_HANDLER_END
}












