#include "stdtypes.h"


AUTO_COMMAND ACMD_ALLOW_JSONRPC ACMD_CATEGORY(Faraday_RPC_PWE) ACMD_NAME("Status.available");
char * frpc_status_available()
{
	return strdup("available");
}