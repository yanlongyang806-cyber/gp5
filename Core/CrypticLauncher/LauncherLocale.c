// CrypticLauncher
#include "LauncherLocale.h"
#include "resource_CrypticLauncher.h"
#include "LauncherMain.h"

// UtilitiesLib
#include "StashTable.h"
#include "earray.h"
#include "StringUtil.h"

// os/system includes
#include "wininclude.h"

// NewControllerTracker
#include "NewControllerTracker_pub.h"

// local var's
static StashTable sMessages = NULL;
static char *sMessagesDataEN = NULL;
static size_t sMessagesDataENSize = 0;
static char *sMessagesData = NULL;
static size_t sMessagesDataSize = 0;

static bool sAvailableLocale[LOCALE_ID_COUNT];

// predec's
static void setMessageTable(LocaleID locID);
static HRSRC findMessageTable(LocaleID locID);

static void addLocaleList(CONST_EARRAY_OF(char) localeNames);

// ==================  PUBLIC FUNCTIONS  ====================

void LauncherSetLocale(LocaleID locID)
{
	setMessageTable(locID);
	setCurrentLocale(locID);
}

bool LauncherIsLocaleAvailable(LocaleID locID)
{
	return sAvailableLocale[locID];
}

LocaleID LauncherChooseFallbackLocale(LocaleID locID)
{
	int i;

	// choose the first available locale
	for (i = 0 ; i < LOCALE_ID_COUNT; i++)
	{
		if (sAvailableLocale[i])
		{
			return (LocaleID)i;
		}
	}

	assertmsg(false, "No locales are available");
	return LOCALE_ID_ENGLISH;
}

void LauncherClearAvailableLocales(void)
{
	int i;

	// clear the cached list
	for (i = 0; i < LOCALE_ID_COUNT; i++)
	{
		sAvailableLocale[i] = false;
	}
}

void LauncherAddAvailableLocale(LocaleID locID)
{
	// make sure the language table exists in the Launcher
	if (findMessageTable(locID))
	{
		sAvailableLocale[locID] = true;
	}
	else
	{
		printf("requested language id %d resources not available!\n", locID);
	}
}

void LauncherAddAvailableLocaleByCode(const char *languageCode)
{
	LocaleID locID = locGetIDByCrypticSpecific2LetterIdentifier(languageCode);
	LauncherAddAvailableLocale(locID);
}

void LauncherResetAvailableLocalesToDefault(const char *productName)
{
	int i;

	// 2013-06-21: Currently, FightClub does not support the "Languages" field.  It is also currently English-only.
	// Therefore, we hardwire it to only offer English.
	if (!stricmp_safe(productName, "FightClub"))
	{
		LauncherClearAvailableLocales();
		sAvailableLocale[LOCALE_ID_ENGLISH] = true;
		return;
	}

	for (i = 0; i < LOCALE_ID_COUNT; i++)
	{
		sAvailableLocale[i] = findMessageTable(i) ? true : false;
	}
}

void LauncherResetAvailableLocalesFromSingleShard(const ShardInfo_Basic *shard)
{
	// If we are in show all games mode, then this will limit languages to only those available 
	// on the currently selected shard.  Changing shards will potentially change languages.
	if (LauncherGetShowAllGamesMode())
	{
		if (shard->ppLanguages)
		{
			LauncherClearAvailableLocales();
			addLocaleList(shard->ppLanguages);
		}
		else
		{
			// fallback, when controller tracker does not provide list of supported languages.
			LauncherResetAvailableLocalesToDefault(shard->pProductName);
		}
	}
}

void LauncherResetAvailableLocalesFromAllShards(CONST_EARRAY_OF(ShardInfo_Basic) shardList)
{
	// If we are not in show all games mode, then this will set available languages once on 
	// receiving the shard list.  All languages will be available, and potentially the shard 
	// will change after changing lanugages.
	if (!LauncherGetShowAllGamesMode())
	{
		if (shardList)
		{
			LauncherClearAvailableLocales();

			FOR_EACH_IN_CONST_EARRAY(shardList, ShardInfo_Basic, shard)
				addLocaleList(shard->ppLanguages);
			FOR_EACH_END
		}
		else
		{
			// fallback, when controller tracker does not provide list of supported languages.
			LauncherResetAvailableLocalesToDefault(gStartupProductName);
		}
	}
}

const char *cgettext(const char *str)
{
	char *trans;

	if (!sMessages)
		return str;

	if (stashFindPointer(sMessages, str, &trans))
		return trans;
	else
		return str;
}

// Load the EN strings at startup
AUTO_RUN;
void loadMessageData(void)
{
	HRSRC rsrc = findMessageTable(LOCALE_ID_ENGLISH);
	if (rsrc)
	{
		HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
		if (gptr)
		{
			sMessagesDataEN = LockResource(gptr); // no need to unlock this ever, it gets unloaded with the program
			sMessagesDataENSize = SizeofResource(GetModuleHandle(NULL), rsrc);
			return;
		}
	}
	assertmsg(0, "Unable to load English message data");
}

// ==================  PRIVATE FUNCTIONS  ====================

static HRSRC findMessageTable(LocaleID locID)
{

	WindowsLocale loc = locGetWindowsLocale(locID);
	WORD langId;
	HRSRC rsrc;

	// FIXME: I don't understand why there is this strange thing that pulls off the primary ID and glues it back onto SUBLANG_DEFAULT.
	// I tend to think that that is just wrong and needs to go away.  But until I understand completely, this hack will be here, to make
	// Chinese work.
	if (loc == LOCALE_CHINESE_SIMPLIFIED)
		langId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
	else
		langId = MAKELANGID(PRIMARYLANGID(loc), SUBLANG_DEFAULT);

	rsrc = FindResourceEx(GetModuleHandle(NULL), L"Messages", MAKEINTRESOURCE(IDR_MESSAGES), langId);
	return rsrc;

}

static void setMessageTable(LocaleID locID)
{
	HRSRC rsrc;
	char *str_en, *str;

	// Early out if already in current locale
	if (locID == getCurrentLocale())
		return;

	// Don't crash if forcing an invalid locale
	if (gForceInvalidLocale && locID == LOCALE_ID_INVALID)
		locID = LOCALE_ID_ENGLISH;

	stashTableDestroy(sMessages);
	sMessages = NULL;
	
	if (locID == 0)
		return;

	rsrc = findMessageTable(locID);
	if (rsrc)
	{
		HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
		if (gptr)
		{
			sMessagesData = LockResource(gptr); // no need to unlock this ever, it gets unloaded with the program
			sMessagesDataSize = SizeofResource(GetModuleHandle(NULL), rsrc);
		}
	}
	assertmsgf(sMessagesDataSize, "Unable to load message data for locale %d", locID);
	
	str_en = sMessagesDataEN;
	str = sMessagesData;

	sMessages = stashTableCreateWithStringKeys(64, StashDefault);

	while(str_en < sMessagesDataEN + sMessagesDataENSize)
	{
		assertmsg(str < sMessagesData + sMessagesDataSize, "Localized message data exhausted before English?");

		stashAddPointer(sMessages, str_en, str, false);

		str_en += strlen(str_en) + 1;
		str += strlen(str) + 1;
	}
}

static void addLocaleList(CONST_EARRAY_OF(char) localeNames)
{
	if (localeNames)
	{
		FOR_EACH_IN_CONST_EARRAY(localeNames, char, localeName)
			int locID = locGetIDByName(localeName);
			if (locID < LOCALE_ID_COUNT)
			{
				LauncherAddAvailableLocale(locID);
			}
		FOR_EACH_END
	}
}

