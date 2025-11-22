//// This file contains all the controls we need to control asset
//// sets.
////
//// Currently it is just an AUTO_SETTING and an AUTO_COMMAND, but it
//// could get much more detailed in the future.

int g_iAssetSetIndexForShard = 0;
int g_iAssetSetIndexOverride = -1;

// Which asset set this shard is using. 0=default
AUTO_CMD_INT(g_iAssetSetIndexForShard, AssetSetIndexForShard) ACMD_AUTO_SETTING(Misc, GAMESERVER);

// Allow overriding the asset set index.  -1 means does not override.
AUTO_CMD_INT(g_iAssetSetIndexOverride, AssetSetIndexOverride) ACMD_COMMANDLINE;

AUTO_EXPR_FUNC(clickable) ACMD_NAME(GetAssetSetIndex);
int gslGetAssetSetIndex(void)
{
	if( g_iAssetSetIndexOverride >= 0 ) {
		return g_iAssetSetIndexOverride;
	} else {
		return g_iAssetSetIndexForShard;
	}
}
