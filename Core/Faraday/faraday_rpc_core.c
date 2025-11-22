#include "stdtypes.h"


AUTO_COMMAND ACMD_ALLOW_JSONRPC ACMD_CATEGORY(Faraday_RPC_PWE) ACMD_NAME("Core.status");
char * frpc_core_status()
{
	return strdup("OK");
}