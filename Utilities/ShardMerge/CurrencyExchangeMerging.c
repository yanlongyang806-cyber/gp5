#include "CurrencyExchangeCommon.h"
#include "CurrencyExchangeMerging.h"
#include "earray.h"
#include "objSchema.h"

#include "AutoGen/CurrencyExchangeCommon_h_ast.h"

AUTO_RUN_LATE;
void RegisterCurrencyExchangeSchema(void)
{
	objRegisterNativeSchema(GLOBALTYPE_CURRENCYEXCHANGE, parse_CurrencyExchangeAccountData, NULL, NULL, NULL, NULL, NULL);
}

// If the user has only one CurrencyExchange container, only the initialize function will run. 
void InitializeCurrencyExchangeContainer(NOCONST(CurrencyExchangeAccountData) *lhs)
{
	if(!lhs)
		return;

	assert(!lhs->forSaleEscrowTC);
	
	// There should not be any open orders
	eaClear(&lhs->openOrders);
}

// This only runs if the player has multiple CurrencyExchange containers. In that case, we clear all logs to avoid merging them
void MergeTwoCurrencyExchangeContainers(NOCONST(CurrencyExchangeAccountData) *lhs, CurrencyExchangeAccountData *rhs)
{
	if(!lhs || !rhs)
		return;

	assert(lhs->iAccountID == rhs->iAccountID);

	// forSaleEscrowTC should be 0, openOrders because all orders should have been cancelled before the beginning of the merge process
	assert(!rhs->forSaleEscrowTC);

	lhs->readyToClaimEscrowTC += rhs->readyToClaimEscrowTC;

	lhs->nextOrderID = MAX(lhs->nextOrderID, rhs->nextOrderID);

	// If there are multiple CurrencyExchange containers, clear the logs.
	eaClear(&lhs->logEntries);
	lhs->logNext = 0;
}
