//// GameServer UGC Search Cache
////
//// GameServer API for insulating UGCSearchManager (and the intershard transactions servers) from heavy load
//// This API turns a UGCProjectSearchInfo into a UGCSearchResult, possibly hitting a cache, possibly requesting
//// data from the UGCSearchManager, and possibly timing out.
#pragma once

typedef struct UGCProjectSearchInfo UGCProjectSearchInfo;
typedef struct Entity Entity;
typedef struct UGCIDList UGCIDList;
typedef struct UGCProjectList UGCProjectList;

void ugcSearchCacheRequest(UGCProjectSearchInfo *pUGCProjectSearchInfo);
void ugcSearchCacheReceive(UGCProjectSearchInfo *pUGCProjectSearchInfo);

void ugcSearchCacheRequestHeaders(Entity *pEntity, UGCIDList* pUGCIDList);
void ugcSearchCacheReceiveHeaders(UGCProjectList *pUGCProjectList);
