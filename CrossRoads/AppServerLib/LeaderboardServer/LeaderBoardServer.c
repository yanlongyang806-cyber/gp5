#include "AppServerLib.h"
#include "aslLeaderboardServerInit.h"

AUTO_RUN;
int RegisterLeaderboardServer(void)
{
	aslRegisterApp(GLOBALTYPE_LEADERBOARDSERVER, LeaderboardServerLibInit, APPSERVERTYPEFLAG_NOPIGGSINDEVELOPMENT);

	return 1;
}