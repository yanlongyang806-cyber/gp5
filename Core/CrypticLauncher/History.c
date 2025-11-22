#include "History.h"
#include "registry.h"
#include "GameDetails.h"

// UtilitiesLib
#include "utils.h"
#include "earray.h"

// Recently launched games
static char **sHistory = NULL;

#define REGKEY_GAME_HISTORY					"GameHistory"

void HistoryLoadFromRegistry(void)
{
	char buf[1024]={0};
	const char *launcherProductDisplayName = gdGetDisplayName(0);

	// Load the game history from the registry
	readRegStr(launcherProductDisplayName, REGKEY_GAME_HISTORY, SAFESTR(buf), false);
	DivideString(buf, ",", &sHistory, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
}

void HistoryEnumerate(HistoryItemCallbackFunc historyItemCallbackFunc, void *userData)
{
	FOR_EACH_IN_EARRAY(sHistory, char, hist)
		if (!historyItemCallbackFunc(hist, userData))
		{
			return;
		}
	FOR_EACH_END
}

void HistoryAddItemAndSaveToRegistry(char *historyItem)
{
	char *regbuf = NULL;
	const char *launcherProductDisplayName = gdGetDisplayName(0);
	int i = eaFindString(&sHistory, historyItem);
	if (i != -1)
		eaRemove(&sHistory, i);
	eaInsert(&sHistory, historyItem, 0);
	if (eaSize(&sHistory) > 10)
		for (i=10; i<eaSize(&sHistory); i++)
			eaRemove(&sHistory, i);

	if (eaSize(&sHistory))
		estrPrintf(&regbuf, "%s", sHistory[0]);
	for (i=1; i<eaSize(&sHistory); i++)
		estrConcatf(&regbuf, ",%s", sHistory[i]);
	writeRegStr(launcherProductDisplayName, REGKEY_GAME_HISTORY, regbuf);
	estrDestroy(&regbuf);
}
