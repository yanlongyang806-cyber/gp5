#pragma once

#include "AppLocale.h"

// predec's
typedef struct ShardInfo_Basic ShardInfo_Basic;

// use this macro to convert from 'string tag' to potentially localized string - use for literal strings.  e.g. _("foo")
#define _(s) cgettext((s))

// use this macro to convert from 'string tag' to potentially localized string - only use this for non literal strings.
extern const char *cgettext(const char *str); // called from multiple threads

// set the locale ID - wraps AppLocale's setCurrentLocale(), and switches launcher localization message tables
extern void LauncherSetLocale(LocaleID locID);

extern bool LauncherIsLocaleAvailable(LocaleID locID);

extern LocaleID LauncherChooseFallbackLocale(LocaleID locID);

extern void LauncherClearAvailableLocales(void);

extern void LauncherAddAvailableLocale(LocaleID locID);
extern void LauncherAddAvailableLocaleByCode(const char *languageCode);

extern void LauncherResetAvailableLocalesToDefault(const char *productName);

extern void LauncherResetAvailableLocalesFromSingleShard(const ShardInfo_Basic* shard);
extern void LauncherResetAvailableLocalesFromAllShards(CONST_EARRAY_OF(ShardInfo_Basic) shardList);
