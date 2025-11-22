#include "fileutil.h"
#include "logging.h"
#include "net.h"
#include "patchcommonutils.h"
#include "patchmirroring.h"
#include "patchserver.h"
#include "patchserver_h_ast.h"
#include "patchupdate.h"
#include "patchxfer.h"
#include "pcl_client.h"
#include "pcl_client_struct.h"
#include "ScratchStack.h"
#include "textparser.h"
#include "timing.h"
#include "windefinclude.h"

#ifdef _M_X64
#	define AUTOUPDATE_TOKEN "PatchServerX64"
#elif defined(WIN32)
#	define AUTOUPDATE_TOKEN "PatchServerWin32"
#else
#	error "Please define AUTOUPDATE_TOKEN for this platform"
#endif

#ifdef ECHO_log_printfS
#define ERROR_PRINTF(format, ...) {log_printf(LOG_PATCHSERVER_ERRORS, format, __VA_ARGS__); printf(format, __VA_ARGS__);}
#define INFO_PRINTF(format, ...) {log_printf(LOG_PATCHSERVER_INFO, format, __VA_ARGS__); printf(format, __VA_ARGS__);}
#else
#define ERROR_PRINTF(...) log_printf(LOG_PATCHSERVER_ERRORS, __VA_ARGS__)
#define INFO_PRINTF(...) log_printf(LOG_PATCHSERVER_INFO, __VA_ARGS__)
#endif

static PCL_Client * s_parentClient = NULL;
static bool s_parentConnected = false;
static bool s_isChildServer = false;
static PCL_ErrorCode s_update_error = PCL_SUCCESS;
static char *s_update_error_details = NULL;
static NetComm *s_pclComm = NULL;
static int s_max_net_bytes = 1024 * 1024 * 5;
static int s_update_log_verbose = 0;

AUTO_CMD_INT(s_update_log_verbose, update_log_verbose);

#define PCL_DO_ERROR()																	\
	do {																				\
		if (error != PCL_SUCCESS)														\
		{																				\
			patchupdateHandlePclError(error, error_details);							\
			return;																		\
		}																				\
	} while(0)

#define PCL_DO(funccall)																\
	do {																				\
		error = (funccall);																\
		PCL_DO_ERROR();																	\
	} while(0)

static void connectToParent(void);

static int sortXferStateInfo(const XferStateInfo **a, const XferStateInfo **b) 
{
	return strcmp((*a)->filename, (*b)->filename);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void cmd_showXferStats(void)
{
	XferStateInfo **info=NULL;
	F32 rec_num, tot_num;
	char *rec_units, *tot_units;
	U32 rec_prec, tot_prec;

	if(!s_parentClient)
	{
		printf("No parent connection\n");
		return;
	}

	eaCreate(&info);
	xferrerGetStateInfo(s_parentClient->xferrer, &info);
	//if(eaSize(&info) == 0)
	//	printf("No transfers active\n");
	eaQSort(info, sortXferStateInfo);
	FOR_EACH_IN_EARRAY(info, XferStateInfo, i)
		humanBytes(i->bytes_requested, &rec_num, &rec_units, &rec_prec);
	printf("%s: %s %u/%u requested: %.*f%s\n", i->filename, i->state, i->blocks_so_far, i->blocks_total, rec_prec, rec_num, rec_units);
	FOR_EACH_END
		humanBytes(s_parentClient->xferrer->net_bytes_free, &rec_num, &rec_units, &rec_prec);
	humanBytes(s_parentClient->xferrer->max_net_bytes, &tot_num, &tot_units, &tot_prec);
	printf("%u/%u transfers\tnet_bytes_free = %.*f%s/%.*f%s\n", eaSize(&info), MAX_XFERS, rec_prec, rec_num, rec_units, tot_prec, tot_num, tot_units);
	eaDestroy(&info);
}

// Low volume update logging
void patchupdateLog_dbg(FORMAT_STR char const *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	log_vprintf(LOG_PATCHSERVER_UPDATE, fmt, ap);
	va_end(ap);
}

// High volume update logging
void patchupdateLogVerbose_dbg(FORMAT_STR char const *fmt, ...)
{
	va_list ap;
	if (s_update_log_verbose)
	{
		va_start(ap, fmt);
		log_vprintf(LOG_PATCHSERVER_UPDATE, fmt, ap);
		va_end(ap);
	}
}

// Report a PCL error, if there is one.
static void patchupdateHandlePclError(PCL_ErrorCode error, const char *details)
{
	bool should_alert;
	const char *error_details = NULL;
	char state_text[256];
	char error_text[256];
	char *msg = NULL;

	// Get error text.
	if (details)
		error_details = details;
	else
		pclGetErrorDetails(s_parentClient, &error_details);
	error_text[0] = 0;
	pclGetErrorString(error, SAFESTR(error_text));
	state_text[0] = 0;
	pclGetStateString(s_parentClient, SAFESTR(state_text));

	// Only alert for certain errors.
	// Don't alert for connection lost, since there's a separate alert, Parent_Connect_Fail, that will happen if we can't reconnect after a certain amount of time.
	should_alert = error != PCL_LOST_CONNECTION;

	// Format error message.
	estrStackCreate(&msg);
	estrPrintf(&msg, "%s%s%s (state %s)", error_text, error_details && *error_details ? ": " : "", NULL_TO_EMPTY(error_details), state_text);

	// Report error.
	ERROR_PRINTF("Error processing patch update from parent: %s\n", msg);
	if (should_alert)
		AssertOrAlert("PATCHDB_REPLICATION_ERROR", "While updating from parent %s:%d, an error occurred: %s",
			g_patchserver_config.parent.server, g_patchserver_config.parent.port ? g_patchserver_config.parent.port : DEFAULT_PATCHSERVER_PORT, msg);
	estrDestroy(&msg);

	// Reset.
	printfColor(COLOR_RED|COLOR_BRIGHT, "\nAn error has occurred while updating, restarting update process.\n\n");
	patchupdateDisconnect();
	s_update_error = PCL_SUCCESS;
	patchmirroringResetConnection();

	// Reconnect.
	connectToParent();
}

// Send update status to the master server.
void patchupdateSendUpdateStatus(const char *status)
{
	devassert(status && *status);
	if (s_parentClient)
		pclPatchServerUpdateStatus(s_parentClient, g_patchserver_config.displayName, status);
}

void parentConnected(PCL_Client * client, bool updated, PCL_ErrorCode error, const char *error_details, void * userData);

// Handle a dynamic Autoupdate patching information update from the parent.
static void autoupInfoUpdate(PCL_Client *client, U32 version, const char *autoup_info, void *userData)
{
	DynamicAutoupConfig *config;
	bool success;

	PERFINFO_AUTO_START_FUNC();

	// Parse the update.
	if(version == AUTOUPINFO_VERSION_0)
	{
		config = StructCreate(parse_DynamicAutoupConfig);
		success = ParserReadText(autoup_info, parse_DynamicAutoupConfig, config, PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE);
		if(!success)
		{
			StructDestroy(parse_DynamicAutoupConfig, config);
			log_printf(LOG_PATCHSERVER_GENERAL, "Could not parse DynamicAutoupConfig: %s", autoup_info);
			AssertOrAlert("BAD_AUTOUP_UPDATE", "Invalid Autoupdate patching update data was received from the parent.");
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
	}
	else
	{
		log_printf(LOG_PATCHSERVER_GENERAL, "Newer Autoupdate info version than supported: %d", version);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Save the update, and propagate it to children.
	log_printf(LOG_PATCHSERVER_GENERAL, "AutoupPatchUpdate()");
	StructDestroy(parse_DynamicAutoupConfig, g_patchserver_config.dynamic_autoup_config);
	g_patchserver_config.dynamic_autoup_config = config;
	patchserverSaveDynamicAutoupConfig();

	PERFINFO_AUTO_STOP_FUNC();
}

// Handle a dynamic HTTP patching information update from the parent.
static void httpInfoUpdate(PCL_Client *client, const char *http_info, void *userData)
{
	DynamicHttpConfig *config;
	bool success;

	PERFINFO_AUTO_START_FUNC();

	// Parse the update.
	config = StructCreate(parse_DynamicHttpConfig);
	success = ParserReadText(http_info, parse_DynamicHttpConfig, config, PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE);
	if (!success)
	{
		log_printf(LOG_PATCHSERVER_GENERAL, "Could not parse DynamicHttpConfig: %s", http_info);
		AssertOrAlert("BAD_HTTP_UPDATE", "Invalid HTTP patching update data was received from the parent.");
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Save the update, and propagate it to children.
	log_printf(LOG_PATCHSERVER_GENERAL, "HttpPatchUpdate()");
	StructDestroy(parse_DynamicHttpConfig, g_patchserver_config.dynamic_http_config);
	g_patchserver_config.dynamic_http_config = config;
	patchserverSaveDynamicHttpConfig();

	PERFINFO_AUTO_STOP_FUNC();
}

// Connect to the parent Patch Server.
static void connectToParent(void)
{
	PCL_ErrorCode error;
	ServerConfig *config = &g_patchserver_config;
	char *server_name = config->parent.server;
	int port = config->parent.port ? config->parent.port : DEFAULT_PATCHSERVER_PORT;

	patchupdateLog("connecting to parent %s on port %i", server_name, port);
	s_isChildServer = true;
	patchmirroringResetConnection();
	error = pclConnectAndCreate(&s_parentClient, server_name, port, INFINITE, s_pclComm, NULL, AUTOUPDATE_TOKEN, NULL, parentConnected, NULL);

	if(error == PCL_SUCCESS)
	{
		error = pclSetCompression(s_parentClient, true);
	}

	if(error == PCL_SUCCESS)
	{
		error = pclSetFileFlags(s_parentClient, PCL_USE_POOLED_PATHS|PCL_CALLBACK_ON_DESTROY);
	}

	if(error == PCL_SUCCESS)
	{
		error = pclSetKeepAliveAndTimeout(	s_parentClient,
			config->parentTimeout ?
			config->parentTimeout :
		5 * 60);
	}

	if(error == PCL_SUCCESS)
	{
		pclPatchServerHandleHttpInfo(s_parentClient, httpInfoUpdate, NULL);
	}

	if(error == PCL_SUCCESS)
	{
		pclPatchServerHandleAutoupInfo(s_parentClient, autoupInfoUpdate, NULL);
	}

	if(error == PCL_SUCCESS)
	{
		pclSetBadFilesDirectory(s_parentClient, "./history/badfiles");
	}

	if(error != PCL_SUCCESS)
	{
		char err_str[MAX_PATH];
		pclGetErrorString(error, SAFESTR(err_str));
		if(error == PCL_LOST_CONNECTION || error == PCL_COMM_LINK_FAILURE)
			Errorf("%s", err_str); // Not a fatal error, but report it anyway
		else
			FatalErrorf("%s", err_str);
	}
}

void parentConnected(PCL_Client * client, bool updated, PCL_ErrorCode error, const char * error_details, void * userData)
{
	patchmirroringResetConnection();

	// FIXME: This slows down sync a good bit.  Instead, we should use the planned new mode that will verify
	// in a background thread.
	// Or maybe turn this on just when we're verifying?
	//if(error == PCL_SUCCESS)
	//	error = pclVerifyAllFiles(s_parentClient, true);

	if(error == PCL_SUCCESS)
		pclSetMaxNetBytes(s_parentClient, s_max_net_bytes);

	if(error == PCL_SUCCESS)
#if _WIN64
		pclSetMaxMemUsage(s_parentClient, 512*1024*1024);
#else
		pclSetMaxMemUsage(s_parentClient, 256*1024*1024);
#endif

	// TODO: add autoupdating

	PCL_DO_ERROR();

	// Once the following activations are sent, we are considered to be connected.
	// This needs to be set first, however, so that the activations will actually be sent.
	s_parentConnected = true;

	// Send active notification.
	if(error == PCL_SUCCESS && g_patchserver_config.serverCategory)
		patchserverResendChildActivations();
	PCL_DO_ERROR();
}

// Start and maintain PCL connection with parent server.
// This should be called once per tick.
void patchupdateProcess()
{
	static time_t startTime, databaseStartTime, checkinStartTime;
	static U32 lastConnected = 0;
	PCL_ErrorCode error;
	const char *error_details = NULL;
	bool progress, continuing;

	PERFINFO_AUTO_START_FUNC();

	// If we have a parent, but we're not connected or connecting, start up a new connection.
	if(!s_parentClient)
	{
		s_parentConnected = false;
		if(g_patchserver_config.parent.server)
			connectToParent();
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Check for errors.
	error = s_update_error;
	error_details = s_update_error_details;
	PCL_DO_ERROR();

	// Process PCL.
	// Lost connections during idle are not considered to be errors.
	error = pclProcessTracked(s_parentClient); // TODO: this can easily go over UPDATE_TIME_PER_FRAME
	if (error == PCL_LOST_CONNECTION && patchmirroringIsMirroringIdle())
	{
		patchupdateLog("disconnected from parent %s on port %i during idle", g_patchserver_config.parent.server, g_patchserver_config.parent.port);
		patchupdateDisconnect();
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	if(error != PCL_SUCCESS && error != PCL_WAITING)
	{
		PERFINFO_AUTO_STOP_FUNC();
		PCL_DO_ERROR();
	}

	// Check for errors.
	error = s_update_error;
	error_details = s_update_error_details;
	PCL_DO_ERROR();

	// Wait until we're connected to proceed.
	if (!s_parentConnected)
	{
		const U32 delay_minutes = 30;
		U32 now = timeSecondsSince2000();
		if (lastConnected && now - lastConnected > delay_minutes * 60)
		{
			AssertOrAlert("Parent_Connect_Fail", "Unable to connect to parent Patch Server after %d minutes", delay_minutes);
			lastConnected = now;
		}
		else if (!lastConnected)
			lastConnected = now;
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	lastConnected = timeSecondsSince2000();

	// Perform PatchDB mirroring.
	continuing = false;
	do {

		// Run one mirroring tick.
		progress = patchmirroringMirrorProcess(s_parentClient, continuing);
		continuing = true;

		// Check for errors.
		error = s_update_error;
		error_details = s_update_error_details;
		PCL_DO_ERROR();

	} while(progress);
	PERFINFO_AUTO_STOP_FUNC();
}

// Return true if this server is not a master server.
bool patchupdateIsChildServer()
{
	return s_isChildServer;
}

// Return true if this server is a child and is connected to a parent.
bool patchupdateIsConnectedToParent()
{
	return g_patchserver_config.parent.server && s_parentClient && s_parentConnected;
}

// Return true if this server is a child and is in the process on connecting or reconnecting to a parent.
bool patchupdateIsConnectingToParent()
{
	return g_patchserver_config.parent.server && (!s_parentClient || !s_parentConnected);
}

// Disconnect from the parent server, if connected.
AUTO_COMMAND;
void patchupdateDisconnect(void)
{
	if (s_parentClient)
		pclDisconnectAndDestroy(s_parentClient);
	s_parentClient = NULL;
	s_parentConnected = false;
}

// Initialize updating.
void patchupdateInit(ServerConfig *config)
{
	if(config->parent.server)
	{
		s_pclComm = commDefault();

		patchmirroringInit();
		connectToParent();
	}
}

// Notify the parent that a child is activating.
void patchupdateNotifyActivate(const char *name, const char *category, const char *parent)
{
	if (patchupdateIsConnectedToParent())
		pclPatchServerActivate(s_parentClient, name, category, parent);
}

// Notify the parent that a child is deactivating.
void patchupdateNotifyDeactivate(const char *name)
{
	if (patchupdateIsConnectedToParent())
		pclPatchServerDeactivate(s_parentClient, name);
}

// Send updating status to the parent.
void patchupdateNotifyUpdateStatus(const char *name, const char *status)
{
	if (patchupdateIsConnectedToParent())
		pclPatchServerUpdateStatus(s_parentClient, name, status);
}

// Notify the parent that a client has accessed a view.
void patchupdateNotifyView(const char * project, const char * name, U32 ip)
{
	if(s_parentClient)
	{
		devassert(name);
		devassert(project);
		devassert(name);
		pclNotifyView(s_parentClient, project, name, ip);
	}
}

// Get PCL max_net_bytes.
U32 patchupdateGetMaxNetBytes()
{
	if (!s_parentClient || !s_parentClient->xferrer || !s_parentClient->xferrer)
		return DEFAULT_MAX_NET_BYTES;
	return s_parentClient->xferrer->max_net_bytes;
}

// Set PCL max_net_bytes.
void patchupdateSetMaxNetBytes(U32 max_net_bytes)
{
	s_max_net_bytes = max_net_bytes;
}

// Set comm used for PCL connections.
void patchupdateSetUpdateComm(NetComm *comm)
{
	s_pclComm = comm;
}

// Report a PCL error.
void patchupdateError(PCL_ErrorCode error, const char *error_details)
{
	// pclGetErrorDetails
	s_update_error = error;
	SAFE_FREE(s_update_error_details);
	if (error_details && *error_details)
		s_update_error_details = strdup(error_details);
}

// Get the current mirroring status.
const char *patchupdateUpdateStatus()
{
	return patchmirrorUpdateStatus(s_parentClient);
}
