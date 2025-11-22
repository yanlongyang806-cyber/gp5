
#include "sysutil.h"
#include "file.h"
#include "memorymonitor.h"
#include "foldercache.h"
#include "winutil.h"
#include "gimmeDLLWrapper.h"
#include "serverlib.h"
#include "UtilitiesLib.h"
#include "ResourceInfo.h"
#include "GenericHttpServing.h"
#include "TimedCallback.h"
#include "StructNet.h"
#include "sock.h"
#include "logging.h"
#include "netsmtp.h"
#include "HttpLib.h"
#include "ControllerLink.h"
#include "timing.h"
#include "Overlord.h"
#include "Overlord_Shards.h"
#include "SimpleStatusMonitoring.h"
#include "Overlord_Clusters.h"
#include "statusReporting_h_ast.h"
#include "overlord_h_Ast.h"
#include "SentryServerComm.h"
#include "..\..\libs\patchclientlib\pcl_client_wt.h"
#include "stringUtil.h"
#include "cmdparse.h"
#include "structDefines.h"

#define OVERLORD_CONFIG_FILE_NAME "c:\\overlord\\config.txt"

OverlordConfig *gpOverlordConfig = NULL;

static void LoadConfig(void)
{
	char *pErrors = NULL;
	char ** ppProducts = NULL;
	gpOverlordConfig = StructCreate(parse_OverlordConfig);

	if (!ParserReadTextFile_CaptureErrors(OVERLORD_CONFIG_FILE_NAME, parse_OverlordConfig, gpOverlordConfig, 0, &pErrors))
	{
		assertmsgf(0, "Unable to load %s: %s\n", OVERLORD_CONFIG_FILE_NAME, pErrors);
	}

	FOR_EACH_IN_EARRAY(gpOverlordConfig->ppShards, OverlordShard, pShard)
	{
		eaPushUnique(&ppProducts, (char*)(pShard->pProductName));
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(gpOverlordConfig->ppClusters, OverlordCluster, pCluster)
	{
		eaPushUnique(&ppProducts, pCluster->pProductName);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(ppProducts, char, pProductName)
	{
		MakePatchVersionsServerMonitorable(gpOverlordConfig->pPatchServer ? gpOverlordConfig->pPatchServer : "patchInternal", 
			pProductName);
	}
	FOR_EACH_END;

	eaDestroy(&ppProducts);

}

void OverlordError(OverlordErrorType eErrorType, const char *pFmt, ...)
{
	char *pFullString = NULL;
	char *pKey = NULL;
	estrGetVarArgs(&pFullString, pFmt);

	estrPrintf(&pKey, "OVERLORD_ERR__%s", StaticDefineInt_FastIntToString(OverlordErrorTypeEnum, eErrorType));

	WARNING_NETOPS_ALERT(pKey, "%s", pFullString);

	estrDestroy(&pKey);
	estrDestroy(&pFullString);
}


int main(int argc,char **argv)
{

	bool bStartedMCPCom = false;

	int		i;
	int frameTimer;

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	SetAppGlobalType(GLOBALTYPE_OVERLORD);
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");


	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'T', 0x8080ff);

	//FolderCacheChooseModeNoPigsInDevelopment();
	FolderCacheChooseMode();
	FolderCacheSetManualCallbackMode(1);

	preloadDLLs(0);

	if (fileIsUsingDevData()) {
	} else {
		gimmeDLLDisable(1);
	}

	srand((unsigned int)time(NULL));
	consoleUpSize(110,128);

	ControllerLink_SetNeverConnect(true);


//	HookReceivedXmlrpcRequests(XMLRPCRequest);

	frameTimer = timerAlloc();

	LoadConfig();
	OverlordClusters_Init();
	OverlordShards_Init();

	gbLogAllAccessLevelCommands = true;
	
	SimpleStatusMonitoring_Begin(OVERLORD_SIMPLE_STATUS_PORT, SSMFLAG_LISTEN_FOR_FORWARDED_STATUSES_AS_OVERLORD);
	GenericHttpServing_Begin(OVERLORD_HTTP_PORT, "OverLord", DEFAULT_HTTP_CATEGORY_FILTER, 0);

	printf("Ready.\n");

//	GenericHttpServing_Begin(CONTROLLERTRACKER_HTTP_PORT, "ControllerTracker", DEFAULT_HTTP_CATEGORY_FILTER, 0);




	//make sure we don't use a log server unless it's specified on the command line
	sprintf(gServerLibState.logServerHost, "NONE");
	serverLibStartup(argc, argv);
	gServerLibState.bAllowErrorDialog = false;

	FolderCacheEnableCallbacks(1);

	for(;;)
	{	

		serverLibOncePerFrame();
		smtpBgThreadingOncePerFrame();

		utilitiesLibOncePerFrame(REAL_TIME);
		FolderCacheDoCallbacks();

		commMonitor(commDefault());
	
		GenericHttpServing_Tick();
		SentryServerComm_Tick();


	}


	EXCEPTION_HANDLER_END



}

char *OVERRIDE_LATELINK_httpServing_GetOverrideHTMLFileForServerMon(const char *pBaseURL, const char *pXPath, UrlArgumentList *pURLArgs)
{

	if (strStartsWith(pBaseURL, "/viewxpath") && stricmp_safe(pXPath, "Overlord[0].custom") == 0
		&& urlFindNonZeroInt(pURLArgs, "pretty") == 1
		&& urlFindNonZeroInt(pURLArgs, "json") == 0
		&& urlFindNonZeroInt(pURLArgs, "xml") == 0)
	{
		return "server/mcp/static_home/overlord/Index.html";
	}

	return NULL;
}

#include "overlord_h_Ast.c"
