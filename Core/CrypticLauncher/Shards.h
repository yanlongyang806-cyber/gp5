#pragma once

// predec's
typedef struct ShardInfo_Basic ShardInfo_Basic;
typedef struct ShardInfo_Basic_List ShardInfo_Basic_List;

// sets up shard list struct, and inits controller tracker comm
extern void ShardsInit(void);

// tick funcs, called in main loop
extern void ShardsAlwaysTickFunc(void);
extern void ShardsLoggedInTickFunc(void);

// called when we connect to a different environnment
extern void ShardsControllerTrackerClearIPs(void);

// returns the number of shards that the CT sent to us
extern int ShardsGetCount(void);

// returns the last shard we ran (NULL if no last shard)
extern const ShardInfo_Basic *ShardsFindLast(void);

// returns the first live shard that is displayed, or just any displayed shard if no live shards are displayed
extern const ShardInfo_Basic *ShardsGetDefault(const char *productName);

// returns the first shard that should be displayed
extern const ShardInfo_Basic *ShardsGetFirstDisplayed();

// returns the shard for the given product and shard names
extern const ShardInfo_Basic *ShardsGetByProductName(const char *productName, const char *shardName);

// returns down or up status for given product (for all of its shards)
extern bool ShardsAreDown(const char *productName);

// returns whether, for this product, we should use buttons when selecting shards, or the pulldown
extern bool ShardsUseButtons(const char *productName);

// returns button states for live, pts1, and pts2 shards for given product
extern void ShardsGetButtonStates(const char *productName, bool *button1, bool *button2, bool *button3);

// retrieve the controller tracker provided msg
extern bool ShardsGetMessage(char **msg);

// culls the internal shard list down to what we want to display
extern bool ShardsPrepareForDisplay(const char *productName, void **userdata); // modifies internal shard list!

// returns true if the shard should be displayed in the shard list
extern bool ShardShouldDisplay(const ShardInfo_Basic *shard);

// returns true if the shard supports the specified locale
extern bool ShardSupportsLocale(const ShardInfo_Basic *shard, int localeId);

// global vars
extern char *gControllerTrackerLastIP; // TODO, make an interface
extern ShardInfo_Basic *gPrePatchShard; // called from patcher
