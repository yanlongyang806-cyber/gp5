#include "AppServerLib.h"
#include "aslAccountProxyServerInit.h"

AUTO_RUN;
int RegisterAccountProxyServer(void)
{
	aslRegisterApp(GLOBALTYPE_ACCOUNTPROXYSERVER, AccountProxyServerLibInit, APPSERVERTYPEFLAG_NOPIGGSINDEVELOPMENT);

	return 1;
}