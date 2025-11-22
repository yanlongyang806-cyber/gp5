#ifndef AUCTIONSERVER_H
#define AUCTIONSERVER_H

#include "GlobalTypeEnum.h"
#include "StashTable.h"

typedef struct AuctionLot AuctionLot;
typedef struct AuctionSearchRequest AuctionSearchRequest;

#define MAXIMUM_MAIL_LOTS 1000

AUTO_STRUCT;
typedef struct SearchTermTable
{
	const char *term;		AST(KEY)
	StashTable tokens;
	U32 hitcount;			AST(NAME(stthc))
} SearchTermTable;

bool Auction_FilterForSearch(AuctionLot *pLot, AuctionSearchRequest *pRequest);

#endif //AUCTIONSERVER_H