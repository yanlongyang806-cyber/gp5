// CrypicLauncher options dialog functions
#pragma once

// Reason prompts for why the options was auto-opened
#define OPTIONS_ERROR_NO_SHARDS_IN_LANGUAGE _("There are no shards available in the current language.  You must change languages to continue.")

// Structure forward defines
typedef struct ShardInfo_Basic ShardInfo_Basic;

extern void ShowOptions(const ShardInfo_Basic *shard, const char* options_reason);
extern void ApplyOptions(const char *jsonStr);

