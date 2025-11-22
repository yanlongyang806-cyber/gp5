#pragma once

//given the name of a shard, return the most recent IP address that we heard from that shard on. Works with
//normal shards and permanent shards. Returns 0 if unrecognized.
U32 GetMostRecentIPFromShardName(const char *pShardName);