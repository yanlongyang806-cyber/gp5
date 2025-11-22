#include "AuctionLot_Transact.h"
#include "AutoGen/AuctionLot_Transact_h_ast.h"

#include "AutoTransDefs.h"
#include "StringFormat.h"

#include "AuctionLot.h"
#include "AutoGen/AuctionLot_h_ast.h"
#include "GamePermissionsCommon.h"

#include "Entity.h"
#include "AutoGen/Entity_h_ast.h"
#include "EntitySavedData.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "Player.h"
#include "AutoGen/Player_h_ast.h"
#include "chatCommonStructs.h"
#include "mailCommon.h"
#include "mailCommon_h_ast.h"

#include "EntityMailCommon.h"
#include "loggingEnums.h"

#ifndef GAMECLIENT
#include "autogen/GameServerLib_autogen_RemoteFuncs.h"
#endif

// Enables auction expiration time extension when an auction is bid on.
static bool s_EnableExpirationExtensionOnBid = false;
AUTO_CMD_INT(s_EnableExpirationExtensionOnBid, EnableExpirationExtensionOnBid) ACMD_AUTO_SETTING(Auction, AUCTIONSERVER, GAMESERVER);

// Causes all auction lots to immediately expire. Will award the item to the highest bidder, if any.
static bool s_TreatExpirationTimeAsNowForAllAuctionLots = false;
AUTO_CMD_INT(s_TreatExpirationTimeAsNowForAllAuctionLots, TreatExpirationTimeAsNowForAllAuctionLots) ACMD_COMMANDLINE;

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pplayerauctiondata.Eaiauctionsbid");
enumTransactionOutcome auction_tr_RemoveExpiredAuctionsFromPlayerBidList(ATR_ARGS, NOCONST(Entity) *pEnt, PlayerAuctionBidListCleanupData *pCleanupData)
{
	if (pCleanupData && ea32Size(&pCleanupData->eaiAuctionLotContainerIDsToRemove) > 0 &&
		NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pPlayerAuctionData))
	{
		S32 i;
		for (i = 0; i < ea32Size(&pCleanupData->eaiAuctionLotContainerIDsToRemove); i++)
		{
			S32 iFoundIndex = -1;
			if (ea32SortedFindIntOrPlace(&pEnt->pPlayer->pPlayerAuctionData->eaiAuctionsBid, pCleanupData->eaiAuctionLotContainerIDsToRemove[i], &iFoundIndex))
			{
				ea32Remove(&pEnt->pPlayer->pPlayerAuctionData->eaiAuctionsBid, iFoundIndex);
			}
		}
	}
	TRANSACTION_RETURN_LOG_SUCCESS("auction_tr_RemoveExpiredAuctionsFromPlayerBidList succeeded.");
}

// Returns the auction expiration time
AUTO_TRANS_HELPER;
U32 auction_trh_GetExpirationTime(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot)
{
	if(s_TreatExpirationTimeAsNowForAllAuctionLots)
		return timeSecondsSince2000() - 1;

	if (NONNULL(pAuctionLot->pBiddingInfo) && s_EnableExpirationExtensionOnBid)
	{
		return pAuctionLot->uExpireTime + (gAuctionConfig.iBidExpirationExtensionInSeconds * pAuctionLot->pBiddingInfo->iNumBids);
	}
	else
	{
		return pAuctionLot->uExpireTime;
	}
}

// Indicates whether the bids are still accepted for the given auction
AUTO_TRANS_HELPER;
bool auction_trh_AcceptsBids(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot)
{
	if (gAuctionConfig.bBiddingEnabled && NONNULL(pAuctionLot) && NONNULL(pAuctionLot->pBiddingInfo) && pAuctionLot->pBiddingInfo->iMinimumBid > 0)
	{
		// Make sure the auction has not expired
		U32 iTimeNow = timeSecondsSince2000();
		U32 iExpireTime = auction_trh_GetExpirationTime(ATR_PASS_ARGS, pAuctionLot);

		if (iTimeNow > iExpireTime)
		{
			return false;
		}

		return true;
	}
	return false;
}

// Returns the minimum numeric value for the next bid
AUTO_TRANS_HELPER;
U32 auction_trh_GetMinimumNextBidValue(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot)
{
	if (gAuctionConfig.bBiddingEnabled && NONNULL(pAuctionLot) && NONNULL(pAuctionLot->pBiddingInfo) && pAuctionLot->pBiddingInfo->iMinimumBid)
	{
		if (pAuctionLot->pBiddingInfo->iCurrentBid)
		{
			U32 iIncrementalValue = MAX(1, round((F32)pAuctionLot->pBiddingInfo->iCurrentBid * gAuctionConfig.fBiddingIncrementalMultiplier));
			return pAuctionLot->pBiddingInfo->iCurrentBid + iIncrementalValue;
		}
		else
		{
			return pAuctionLot->pBiddingInfo->iMinimumBid;
		}
		return pAuctionLot->pBiddingInfo->iCurrentBid == 0 ? pAuctionLot->pBiddingInfo->iMinimumBid : pAuctionLot->pBiddingInfo->iCurrentBid + 1;
	}

	return 0;
}

// Indicates whether the auction can be owned by paying the buyout price
AUTO_TRANS_HELPER;
bool auction_trh_AcceptsBuyout(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot)
{
	if (pAuctionLot->price == 0)
	{
		// No price is specified
		return false;
	}

	if (ISNULL(pAuctionLot->pBiddingInfo))
	{
		// If there is no bidding information, the auction is only available for buyout.
		return true;
	}

	// Auction can be bought out as long as the current bid is less than the buyout price
	return pAuctionLot->pBiddingInfo->iCurrentBid < pAuctionLot->price;
}

// Indicates whether the auction can be canceled
AUTO_TRANS_HELPER;
bool auction_trh_CanBeCanceled(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot)
{
	if (NONNULL(pAuctionLot->pBiddingInfo))
	{
		return !pAuctionLot->pBiddingInfo->iNumBids;
	}
	return true;
}

// Calculates the final sales fee. This function already takes the posting fee paid into account.
AUTO_TRANS_HELPER;
U32 auction_trh_GetFinalSalesFee(ATR_ARGS, ATH_ARG NOCONST(Entity) *pAuctionOwnerEnt, ATH_ARG NOCONST(AuctionLot) *pAuctionLot, bool bBuyout)
{
	U32 iSalesFee;
	U32 iFinalSalesPrice;
	AuctionDurationOption *pDurationOption;

	if(!gAuctionConfig.bAuctionsUseSoldFee)
	{
		// no fee
		return 0;
	}

	// Get the duration option to see if a custom duration option is used for the auction
	pDurationOption = Auction_GetDurationOption(pAuctionLot->uExpireTime - pAuctionLot->creationTime);

	// Set the final sales price	
	if (!bBuyout && NONNULL(pAuctionLot->pBiddingInfo) && pAuctionLot->pBiddingInfo->iCurrentBid)
	{
		iFinalSalesPrice = pAuctionLot->pBiddingInfo->iCurrentBid;
	}
	else
	{
		iFinalSalesPrice = pAuctionLot->price;
	}

	// get the sold fee, rounding down intentionally
	if (pDurationOption && pDurationOption->fAuctionSoldFee)
	{
		iSalesFee = iFinalSalesPrice * pDurationOption->fAuctionSoldFee;
	}
	else
	{
		iSalesFee = iFinalSalesPrice * gAuctionConfig.fAuctionDefaultSoldFee;
	}	

	//
	// This is where account / character price changes should be set
	//
	if(NONNULL(pAuctionOwnerEnt) && gamePermission_Enabled())
	{
		bool bFound;
		S32 iVal = GamePermissions_trh_GetCachedMaxNumericEx(pAuctionOwnerEnt, GAME_PERMISSION_AUCTION_SOLD_PERCENT, false, &bFound);
		if(bFound)
		{
			F32 fPercent = ((float)iVal) / 100.0f ;
			iSalesFee = min(iFinalSalesPrice * fPercent, iSalesFee);
		}
	}

	if (pAuctionLot->bWasPostedRemotely)
	{
		iSalesFee *= gAuctionConfig.fPlayerRoamingFeeMultiplier;
	}
	// reduce by posting fee
	if(pAuctionLot->uPostingFee < iSalesFee)
	{
		iSalesFee -= pAuctionLot->uPostingFee;
	}
	else
	{
		// posting fee >= uSoldFee
		iSalesFee = 0; 
	}

	// Sales fee cannot be more than the sales price
	iSalesFee = min(iSalesFee, iFinalSalesPrice);

	return iSalesFee;
}

#ifndef GAMECLIENT

// Called to close an auction (works for bids and buyouts)
AUTO_TRANS_HELPER;
bool auction_trh_CloseAuction(ATR_ARGS, 
	ATH_ARG NOCONST(Entity) *pOwner, 
	ATH_ARG NOCONST(Entity) *pBuyer,  
	ATH_ARG NOCONST(AuctionLot) *pLot,
	bool bBuyout,
	const ItemChangeReason *pSellerReason,
	const ItemChangeReason *pBuyerReason)
{
	char *estrAuctionWonBody = NULL;
	char *estrAuctionSoldBody = NULL;
	char *estrDetails = NULL;
	bool bWonEmailSent;
	bool bSoldEmailSent;
	U32 iSoldPrice;
	NOCONST(EmailV3Message)* pMessage = NULL;
	MailCharacterItems *pMailItems = NULL;
    char *ownerName = "";
    char *ownerAccountName = "";
    char *buyerName = "";
    char *buyerAccountName = "";
    U32 iSoldPriceAfterFees = 0;

	if (pLot->state != ALS_Open)
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, auction is no longer open");
		return false;
	}

	if (bBuyout)
	{
        if ( ISNULL(pBuyer) )
        {
            TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, buyer is missing on buyout");
            return false;
        }

		// Make sure that the pBuyer is not the same player as the auction owner
		if (pBuyer->myContainerID == pLot->ownerID)
		{
			TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, pBuyer[%d] is the auction owner.", pBuyer->myContainerID);
			return false;
		}

		if (pLot->price > INT_MAX)
		{
			TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, lot price more than INT_MAX.");
			return false;
		}

		iSoldPrice = pLot->price;

		// Set the state to clean up
		pLot->state = ALS_Cleanup;

		// Charge the buyer
		// audit: Buyout deducts here, but a bid deducts elsewhere?
		if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pBuyer, false, Auction_GetCurrencyNumeric(), -((S32)pLot->price), pBuyerReason))
		{
			TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, cannot charge the buyer.");
			return false;
		}
	}
	else
	{
		if (ISNULL(pLot->pBiddingInfo))
		{
			TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, there is no bidding information.");
			return false;
		}

		// Make sure that the pBuyer is the same player as the last bidder
		if (NONNULL(pBuyer) && pLot->pBiddingInfo->iBiddingPlayerContainerID != pBuyer->myContainerID)
		{
			TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, pBuyer[%d] is different than the last bidder[%d].", 
				pBuyer->myContainerID,
				pLot->pBiddingInfo->iBiddingPlayerContainerID);
			return false;
		}

		iSoldPrice = pLot->pBiddingInfo->iCurrentBid;

		// Set the state to clean up
		pLot->state = ALS_Cleanup_BiddingClosed;
	}

	if (iSoldPrice > INT_MAX)
	{
		//This lot has a negative price. That's terrible, and should have prevented this transaction from being called in the first place.
		TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, bid price was negative.");
		return false;
	}

    if ( NONNULL(pBuyer) )
    {
        buyerName = pBuyer->pSaved->savedName;
        buyerAccountName = pBuyer->pPlayer->publicAccountName;
    }

    if ( NONNULL(pOwner) )
    {
        U32 iSoldFee;
		NOCONST(AuctionSlot)* pSlot = NONNULL(pLot) && pLot->ppItemsV2 ? pLot->ppItemsV2[0] : NULL;
		const char* pchTranslatedItemName = NULL;

		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pLot->ppItemsV2, NOCONST(AuctionSlot), pAuctionSlot)
		{
			S32 iTranslatedNameCount = eaSize(&pAuctionSlot->ppTranslatedNames);
			if (iTranslatedNameCount > 0)
			{
				S32 iLangID = pLot->iLangID;
				if (iLangID >= iTranslatedNameCount)
				{
					iLangID = 0;
				}
				pchTranslatedItemName = pAuctionSlot->ppTranslatedNames[iLangID];
				break;
				//we don't support auctionlots with more than one item.
			}
			else
			{
				pchTranslatedItemName = "[Untranslated Item]";
			}
		}

		FOR_EACH_END
	    // Calculate the sold price after fees
	    iSoldFee = auction_trh_GetFinalSalesFee(ATR_PASS_ARGS, pOwner, pLot, bBuyout);
	    iSoldPriceAfterFees = iSoldPrice - iSoldFee;

	    // Pay the auction owner
	    if (iSoldPriceAfterFees)
	    {
		    if (!gAuctionConfig.bIncludeCurrencyInSoldEmail && !gAuctionConfig.bMailAllItemsAndCurrency)
		    {
			    if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pOwner, false, Auction_GetCurrencyNumeric(), iSoldPriceAfterFees, pSellerReason))
			    {
				    TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, could not pay the owner[%d].", pOwner->myContainerID);
				    return false;
			    }
		    }
		    else
		    {
			    NOCONST(Item)* pCurrency = inv_ItemInstanceFromDefName(Auction_GetCurrencyNumeric(), 0, 0, NULL, NULL, NULL, false, NULL);
			    pCurrency->count = iSoldPriceAfterFees;
			    pMailItems = CharacterMailAddItem(NULL, CONTAINER_RECONST(Item, pCurrency));
		    }
	    }
	    langFormatMessageKey(pLot->iLangID, &estrAuctionSoldBody, "Auction_Sold_Body",
		    STRFMT_STRING("Name", buyerName), 
		    STRFMT_STRING("Account", buyerAccountName),
		    STRFMT_INT("Price", iSoldPrice),
		    STRFMT_INT("PriceAfterFees", MAX(0, iSoldPriceAfterFees - pLot->uPostingFee)),
			STRFMT_INT("SoldFee", iSoldFee),
			STRFMT_INT("PostingFee", pLot->uPostingFee),
			STRFMT_STRING("ItemName", pchTranslatedItemName),
			STRFMT_INT("ItemQuantity", pSlot && pSlot->slot.pItem ? pSlot->slot.pItem->count : 0),
		    STRFMT_END);
	
	    bSoldEmailSent = EntityMail_trh_NPCAddMail(ATR_PASS_ARGS, pOwner,
		    langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Sold_From_Name", "[UNTRANSLATED]Auction House"),
		    langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Sold_Subject", "[UNTRANSLATED]Your auction is sold."), 
		    estrAuctionSoldBody, 
		    pMailItems,
		    0,
		    kNPCEmailType_Default,
		    pSellerReason);

	    estrDestroy(&estrAuctionSoldBody);

	    if (!bSoldEmailSent)
	    {
		    TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, \"Auction Sold\" email could not be sent to the auction owner.");
		    return false;
	    }

        ownerName = pOwner->pSaved->savedName;
        ownerAccountName = pOwner->pPlayer->publicAccountName;

        if (gAuctionConfig.bIncludeCurrencyInSoldEmail || gAuctionConfig.bMailAllItemsAndCurrency)
            EntityMail_trh_CheckCurrencySum(ATR_PASS_ARGS, pOwner, gAuctionConfig.pchCurrencyNumeric);
    }

    if ( NONNULL(pBuyer) )
    {
	    langFormatMessageKey(pBuyer->pPlayer->langID, &estrAuctionWonBody, "Auction_Won_Body",
		    STRFMT_STRING("Name", ownerName), 
		    STRFMT_STRING("Account", ownerAccountName),
		    STRFMT_INT("Price", iSoldPrice),
		    STRFMT_END);

	    // Send the "Auction Won" email
	    pMailItems = CharacterMailAddItemsFromAuctionLot(NULL, pLot);
	    bWonEmailSent = EntityMail_trh_NPCAddMail(ATR_PASS_ARGS, pBuyer,
		    langTranslateMessageKeyDefault(pBuyer->pPlayer->langID, "Auction_Won_From", "[UNTRANSLATED]Auction House"),
		    langTranslateMessageKeyDefault(pBuyer->pPlayer->langID, "Auction_Won_Subject", "[UNTRANSLATED]You have won an auction!"), 
		    estrAuctionWonBody, 
		    pMailItems,
		    0,
		    kNPCEmailType_Default,
		    pBuyerReason);

	    estrDestroy(&estrAuctionWonBody);

        if (!bWonEmailSent)
	    {
		    TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, \"Auction Won\" email could not be sent to the last bidder.");
		    return false;
	    }
    }

	if (bBuyout)
		AuctionLot_trh_GetBuyoutSaleLogDetailString(pLot, pBuyer, iSoldPriceAfterFees, &estrDetails);
	else
		AuctionLot_trh_GetBidSaleLogDetailString(pLot, iSoldPriceAfterFees, &estrDetails);

	TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_AUCTION, "Auction_Purchased", "%s", estrDetails);

	estrDestroy(&estrDetails);

	return true;
}

AUTO_TRANSACTION
	ATR_LOCKS(pOwner, ".Pplayer.Pemailv2.Unextfuturedelivery, .Pplayer.Pemailv2.Bunreadmail, .Pplayer.Pemailv2.Mail, .Pplayer.Pemailv2.Ilastusedid, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], .Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[], .Pplayer.Eaplayernumericthresholds")
	ATR_LOCKS(pLastBidder, ".Pplayer.Pemailv2.Unextfuturedelivery, .Pplayer.Pemailv2.Bunreadmail, .Pplayer.Accountid, .Pplayer.Pemailv2.Mail, .Pplayer.Pemailv2.Ilastusedid, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], .Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Langid, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[], .Pplayer.Eaplayernumericthresholds")
	ATR_LOCKS(pLot, ".Bwaspostedremotely, .Pbiddinginfo.Ibiddingplayeraccountid, .Pbiddinginfo.Iminimumbid, .Owneraccountid, .Icontainerid, .State, .Price, .Ownerid, .Pbiddinginfo.Ibiddingplayercontainerid, .Ilangid, .Ppitemsv2, .Pbiddinginfo.Icurrentbid, .Upostingfee, .Uexpiretime, .Creationtime");
enumTransactionOutcome auction_tr_ExpireAuctionWithLastBidder(ATR_ARGS, NOCONST(Entity) *pOwner, NOCONST(Entity) *pLastBidder, NOCONST(AuctionLot) *pLot, const ItemChangeReason *pSellerReason, const ItemChangeReason *pBuyerReason)
{
	NOCONST(Item)* pItem = NULL;

	if(pLot->state == ALS_Mailed)
	{
		if(pLot->price == 0)
		{
			TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionWithLastBidder failed, mailed auction doesn't have price.");
		}
	}
	else if (pLot->state != ALS_Open) {
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionWithLastBidder failed, auction isn't active.");
	}

    if ( NONNULL(pOwner) )
    {
	    if (pOwner->myContainerID != pLot->ownerID) {
		    TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionWithLastBidder failed, wrong owner passed in.");
	    }
    }

    if(ISNULL(pOwner) && ISNULL(pLastBidder))
    {
        TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionWithLastBidder failed, owner and bidder both don't exist.");
    }

    if(NONNULL(pOwner) && NONNULL(pLastBidder) &&
        pOwner->myContainerID == pLastBidder->myContainerID)
    {
	    TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionWithLastBidder failed, owner and bidder are the same entity.");
    }

	if (NONNULL(pLastBidder) &&
		NONNULL(pLot->pBiddingInfo) && 
		pLot->pBiddingInfo->iBiddingPlayerContainerID != pLastBidder->myContainerID)
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionWithLastBidder failed, wrong bidder passed in.");
	}

	if (NONNULL(pLastBidder) && pLot->state != ALS_Open)
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionWithLastBidder failed, auction lot in wrong state.");
	}

	// audit: pLot is not checked to see that it is populated and that there is at least one bid.

	// There was at least one bid for this auction. The auction goes to the last bidder instead of returning to the owner
	if (auction_trh_CloseAuction(ATR_PASS_ARGS, pOwner, pLastBidder, pLot, false, pSellerReason, pBuyerReason))
	{
		TRANSACTION_RETURN_LOG_SUCCESS("Lot bidding is closed successfully.");
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE("Lot bidding could not be closed properly.")
	}
}

AUTO_TRANS_HELPER;
bool auction_trh_SendMailWithCurrency(ATR_ARGS, ATH_ARG NOCONST(Entity)* pOldBidder, 
	ATH_ARG NOCONST(Entity)* pNewBidder, 
	ATH_ARG NOCONST(AuctionLot) *pAuctionLot, 
	const char* pchMessageBody,
	const char* pchMessageSubject,
	U32 iTransactionAmount, bool bIncludeCurrency,
	const ItemChangeReason* pReason)
{
	bool bReturnValue = true;
	char *estrBody = NULL;
	MailCharacterItems* pMailItems = NULL;
	const char* pchNewWinnerName = SAFE_MEMBER2(pNewBidder, pSaved, savedName);
	const char* pchNewWinnerAccount = SAFE_MEMBER2(pNewBidder, pPlayer, publicAccountName);
	const char* pchTranslatedItemName = NULL;
	int iCount = 0;

	if (NONNULL(pAuctionLot) && NONNULL(pAuctionLot->ppItemsV2[0]) && NONNULL(pAuctionLot->ppItemsV2[0]->slot.pItem))
		iCount = pAuctionLot->ppItemsV2[0]->slot.pItem->count;

	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pAuctionLot->ppItemsV2, NOCONST(AuctionSlot), pAuctionSlot)
	{
		S32 iTranslatedNameCount = eaSize(&pAuctionSlot->ppTranslatedNames);
		if (iTranslatedNameCount > 0)
		{
			S32 iLangID = pAuctionLot->pBiddingInfo->iBiddingPlayerLangID;
			if (iLangID >= iTranslatedNameCount)
			{
				iLangID = 0;
			}
			pchTranslatedItemName = pAuctionSlot->ppTranslatedNames[iLangID];
			break;
			//we don't support auctionlots with more than one item.
		}
		else
		{
			pchTranslatedItemName = "[Untranslated Item]";
		}
	}
	FOR_EACH_END

		langFormatMessageKey(pAuctionLot->pBiddingInfo->iBiddingPlayerLangID, &estrBody, pchMessageBody, 
		STRFMT_STRING("Name", NULL_TO_EMPTY(pchNewWinnerName)), 
		STRFMT_STRING("Account", NULL_TO_EMPTY(pchNewWinnerAccount)),
		STRFMT_STRING("ItemName", NULL_TO_EMPTY(pchTranslatedItemName)),
		STRFMT_INT("ItemCount", iCount), 
		STRFMT_INT("NewBid", pAuctionLot->pBiddingInfo->iCurrentBid), 
		STRFMT_INT("OldBid", iTransactionAmount), 
		STRFMT_END);


	if (bIncludeCurrency)
	{
		NOCONST(Item)* pCurrency = inv_ItemInstanceFromDefName(Auction_GetCurrencyNumeric(), 0, 0, NULL, NULL, NULL, false, NULL);
		pCurrency->count = iTransactionAmount;
		pMailItems = CharacterMailAddItem(NULL, CONTAINER_RECONST(Item, pCurrency));
	}
	bReturnValue = EntityMail_trh_NPCAddMail(ATR_PASS_ARGS, pOldBidder,
		langTranslateMessageKeyDefault(pAuctionLot->pBiddingInfo->iBiddingPlayerLangID, "Auction_Outbid_From_Name", "[UNTRANSLATED]Auction House"),
		langTranslateMessageKeyDefault(pAuctionLot->pBiddingInfo->iBiddingPlayerLangID, pchMessageSubject, "[UNTRANSLATED]Auction lot outbid."), 
		estrBody,
		pMailItems,
		0,
		kNPCEmailType_Default,
		pReason);

	if (bIncludeCurrency)
		EntityMail_trh_CheckCurrencySum(ATR_PASS_ARGS, pOldBidder, gAuctionConfig.pchCurrencyNumeric);

	estrDestroy(&estrBody);

	return bReturnValue;
}

AUTO_TRANS_HELPER;
bool auction_trh_HandleOutbid(ATR_ARGS, ATH_ARG NOCONST(Entity) *pBidder, ATH_ARG NOCONST(Entity) *pWinner, ATH_ARG NOCONST(AuctionLot)* pAuctionLot, U32 uiBid, const ItemChangeReason *pOutbidReason)
{
	bool bRet = true;

	if (!gAuctionConfig.bMailAllItemsAndCurrency)
		bRet = inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pBidder, false, Auction_GetCurrencyNumeric(), uiBid, pOutbidReason);

	// Send the old bidder an email to indicate that they are outbid by the new bidder
	bRet = bRet && auction_trh_SendMailWithCurrency(ATR_PASS_ARGS, pBidder, pWinner, pAuctionLot, "Auction_Outbid_Body", "Auction_Outbid_Subject", uiBid, gAuctionConfig.bMailAllItemsAndCurrency, pOutbidReason);

	return bRet;
}

AUTO_TRANSACTION
	ATR_LOCKS(pOwner, ".Pplayer.Pemailv2.Unextfuturedelivery, .Pplayer.Pemailv2.Bunreadmail, .Pplayer.Pemailv2.Mail, .Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Pemailv2.Ilastusedid, .Pplayer.Accountid, .Pplayer.Langid")
	ATR_LOCKS(pLastBidder, ".Pplayer.Pemailv2.Unextfuturedelivery, .Pplayer.Pemailv2.Bunreadmail, .Pplayer.Pemailv2.Ilastusedid, .Pplayer.Pemailv2.Mail, .Pplayer.Eaplayernumericthresholds, .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pLot, ".State, .Price, .Ownerid, .Pbiddinginfo.Ibiddingplayercontainerid, .Ilangid, .Ppitemsv2, .Pbiddinginfo.Icurrentbid, .Pbiddinginfo.Iminimumbid, .Owneraccountid, .Icontainerid, .Recipientid, .Pbiddinginfo.Ibiddingplayerlangid");
enumTransactionOutcome auction_tr_ForceExpireAuction(ATR_ARGS, NOCONST(Entity) *pOwner, NOCONST(Entity) *pLastBidder, NOCONST(AuctionLot) *pLot, const ItemChangeReason *pBuyerReason,const ItemChangeReason *pSellerReason)
{
	NOCONST(Item)* pItem = NULL;
	char* estrDetails = NULL;

	bool bExpiredEmailSent;
	MailCharacterItems* pMailItems;

	if(ISNULL(pLot))
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ForceExpireAuction failed, auction lot no longer exists.");

	if(pLot->state == ALS_Mailed)
	{
		if(pLot->price == 0)
		{
			TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ForceExpireAuction failed, mailed auction doesn't have price.");
		}
	}
	else if (pLot->state != ALS_Open) {
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ForceExpireAuction failed, auction isn't active.");
	}

	if (NONNULL(pOwner) && pOwner->myContainerID != pLot->ownerID) {
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ForceExpireAuction failed, wrong owner passed in.");
	}

	if (NONNULL(pLastBidder) &&
		NONNULL(pLot->pBiddingInfo) && 
		pLot->pBiddingInfo->iBiddingPlayerContainerID != pLastBidder->myContainerID)
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ForceExpireAuction failed, wrong bidder passed in.");
	}

	if (NONNULL(pLastBidder) && pLot->state != ALS_Open)
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ForceExpireAuction failed, auction lot in wrong state.");
	}
	
	if (NONNULL(pLastBidder) && NONNULL(pLot->pBiddingInfo))
	{
		if (!auction_trh_HandleOutbid(ATR_PASS_ARGS, pLastBidder, pOwner, pLot, pLot->pBiddingInfo->iCurrentBid, pBuyerReason))
		{
			TRANSACTION_RETURN_LOG_FAILURE(
				"auction_tr_ForceExpireAuction failed:  Currency not returned to high bidder.");
		}
	}

	if (NONNULL(pOwner))
	{
		pLot->recipientID = pOwner->pPlayer->accountID;
		pLot->iLangID = pOwner->pPlayer->langID;

		pMailItems = CharacterMailAddItemsFromAuctionLot(NULL, pLot);

		bExpiredEmailSent = EntityMail_trh_NPCAddMail(ATR_PASS_ARGS, pOwner,
			langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Sold_From_Name", "Auction House"),
			langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Expired_Subject", "Auction lot expired"),
			langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Expired_Body", "This lot has expired"), 
			pMailItems,
			0,
			kNPCEmailType_ExpiredAuction,
			NULL);

		if (!bExpiredEmailSent)
		{
			TRANSACTION_RETURN_LOG_FAILURE(
				"Failed:  Expired auction not lot added to character mail.");
		}
	}

	AuctionLot_trh_GetGenericLogDetailString(pLot, pLot->iContainerID, &estrDetails);

	TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_AUCTION, "Auction_Force_Expired", "%s", estrDetails);

	estrDestroy(&estrDetails);

	TRANSACTION_RETURN_LOG_SUCCESS("Lot forcibly expired.");
}

AUTO_TRANSACTION
	ATR_LOCKS(pOwner, ".Pplayer.Pemailv2.Unextfuturedelivery, .Pplayer.Pemailv2.Bunreadmail, .Pplayer.Pemailv2.Mail, .Pplayer.Pemailv2.Ilastusedid, .Pplayer.Accountid, .Pplayer.Langid")
	ATR_LOCKS(pLot, ".Pbiddinginfo.Icurrentbid, .Pbiddinginfo.Iminimumbid, .Owneraccountid, .Icontainerid, .State, .Price, .Ownerid, .Pbiddinginfo.Ibiddingplayercontainerid, .Recipientid, .Ilangid, .Ppitemsv2");
enumTransactionOutcome auction_tr_ExpireAuctionNoBids(ATR_ARGS, NOCONST(Entity) *pOwner, NOCONST(AuctionLot) *pLot)
{
	NOCONST(Item)* pItem = NULL;
	char* estrDetails = NULL;

	bool bExpiredEmailSent;
	MailCharacterItems* pMailItems;

	if(pLot->state == ALS_Mailed)
	{
		if(pLot->price == 0)
		{
			TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionNoBids failed, mailed auction doesn't have price.");
		}
	}
	else if (pLot->state != ALS_Open) {
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionNoBids failed, auction isn't active.");
	}

	if (ISNULL(pOwner) || ISNULL(pOwner->pPlayer)) {
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionNoBids failed, owner is NULL or not a player.");
	}

	if (pOwner->myContainerID != pLot->ownerID) {
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionNoBids failed, wrong owner passed in.");
	}

	if (NONNULL(pLot->pBiddingInfo) && 
		pLot->pBiddingInfo->iBiddingPlayerContainerID != 0 &&
		pLot->pBiddingInfo->iCurrentBid > 0 && 
		pLot->pBiddingInfo->iCurrentBid <= INT_MAX)
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionNoBids failed, the auction had a last bidder and valid current bid. Use auction_tr_ExpireAuctionWithLastBidder() instead for this case.");
	}

	if(pLot->state == ALS_Mailed)
	{
		pLot->state = ALS_Cleanup_Mailed;
	}
	else
	{
		pLot->state = ALS_Cleanup;
	}
	pLot->recipientID = pOwner->pPlayer->accountID;
	pLot->iLangID = pOwner->pPlayer->langID;

	pMailItems = CharacterMailAddItemsFromAuctionLot(NULL, pLot);

	bExpiredEmailSent = EntityMail_trh_NPCAddMail(ATR_PASS_ARGS, pOwner,
		langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Sold_From_Name", "Auction House"),
		langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Expired_Subject", "Auction lot expired"),
		langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Expired_Body", "This lot has expired"), 
		pMailItems,
		0,
		kNPCEmailType_ExpiredAuction,
		NULL);

	if (!bExpiredEmailSent)
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Failed:  Expired auction not lot added to character mail.");
	}

	AuctionLot_trh_GetGenericLogDetailString(pLot, pLot->iContainerID, &estrDetails);

	TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_AUCTION, "Auction_Expired", "%s", estrDetails);

	estrDestroy(&estrDetails);

	TRANSACTION_RETURN_LOG_SUCCESS("Lot expired.");
}

AUTO_TRANSACTION
ATR_LOCKS(pLot, ".Ppitemsv1_Deprecated, .Uversion, .Ppitemsv2");
enumTransactionOutcome auction_tr_AuctionSlotItemFixup(ATR_ARGS, NOCONST(AuctionLot) *pLot)
{
	if(NONNULL(pLot->ppItemsV1_Deprecated))
	{
		NOCONST(Item)* pItem = NULL;
		int i = 0;
		for (i = 0; i < eaSize(&pLot->ppItemsV1_Deprecated); i++)
		{
			NOCONST(AuctionSlotV1)* pOldSlot = pLot->ppItemsV1_Deprecated[i];
			NOCONST(AuctionSlot)* pNewSlot = StructCreateNoConst(parse_AuctionSlot);
			pNewSlot->eUICategory = pOldSlot->eUICategory;
			pNewSlot->iItemSortType = pOldSlot->iItemSortType;
			pNewSlot->pchItemSortTypeCategoryName = pOldSlot->pchItemSortTypeCategoryName;
			pNewSlot->ppTranslatedNames = pOldSlot->ppTranslatedNames;
			pOldSlot->ppTranslatedNames = NULL;
			inv_trh_ent_MigrateSlotV1ToV2(ATR_PASS_ARGS, (NOCONST(InventorySlotV1)*)pOldSlot, (NOCONST(InventorySlot)*)pNewSlot);
			eaPush(&pLot->ppItemsV2, pNewSlot);
		}

		eaDestroyStructNoConst(&pLot->ppItemsV1_Deprecated, parse_AuctionSlotV1);
		pLot->ppItemsV1_Deprecated = NULL;
	}

	pLot->uVersion = AUCTION_LOT_VERSION;

	TRANSACTION_RETURN_LOG_SUCCESS("Lot Fixup Successful.");
}

AUTO_TRANSACTION
ATR_LOCKS(pLot, ".Ppitemsv2, .Ilangid")
ATR_LOCKS(pEnt, ".Pplayer.Pemailv2.Bunreadmail, .Pplayer.Pemailv2.Unextfuturedelivery, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.pEmailV2.Mail");
enumTransactionOutcome auction_tr_SendAuctionLotThroughMail(ATR_ARGS, NOCONST(AuctionLot) *pLot, NOCONST(Entity) *pEnt)
{
	if (ISNULL(pLot) || ISNULL(pEnt))
	{
		TRANSACTION_OUTCOME_FAILURE;
	}

	if (NONNULL(pEnt->pPlayer))
	{
		int i;
		bool bEmailSent = false;
		MailCharacterItems* pCharacterItems = NULL;

		for (i = 0; i < eaSize(&pLot->ppItemsV2); i++)
		{
			pCharacterItems = CharacterMailAddItem(pCharacterItems, StructCloneReConst(parse_Item, pLot->ppItemsV2[i]->slot.pItem));
		}

		bEmailSent = EntityMail_trh_NPCAddMail(ATR_PASS_ARGS, pEnt,
			langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Sold_From_Name", "[UNTRANSLATED]Auction House"),
			langTranslateMessageKeyDefault(pLot->iLangID, "Auction_MailedLot_Subject", "[UNTRANSLATED]Auction lot expired."), 
			langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Expired_Body", "This lot has expired"), 
			pCharacterItems,
			0,
			kNPCEmailType_ExpiredAuction, NULL);

		if (bEmailSent)
			return TRANSACTION_OUTCOME_SUCCESS;
	}

	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(eaAuctionLots, ".Icontainerid, .Ilangid, .Ppitemsv2, .state")
ATR_LOCKS(pSharedBank, ".Pemailv3.Ulastusedid, .Pemailv3.Bunreadmail, .Pemailv3.Imessagecount, .Pemailv3.Iattachmentscount, .Pemailv3.Eamessages");
enumTransactionOutcome auction_tr_MailLostLots(ATR_ARGS, CONST_EARRAY_OF(NOCONST(AuctionLot)) eaAuctionLots, NOCONST(Entity) *pSharedBank)
{
	U32 uNow = timeSecondsSince2000();
	S32 i, k;
	char *estrSubject = NULL;

	if(ISNULL(pSharedBank->pEmailV3))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	estrCreate(&estrSubject);

	for (i = 0; i < eaSize(&eaAuctionLots); i++)
	{
		NOCONST(EmailV3Message)* pEmailMessage = StructCreateNoConst(parse_EmailV3Message);
		const char *pchMessageFrom = langTranslateMessageKeyDefault(eaAuctionLots[i]->iLangID, "Auction_MailLostLot_From", "[UNTRANSLATED]Mail Attachment Recovery");
		const char *pchMessageBody = langTranslateMessageKeyDefault(eaAuctionLots[i]->iLangID, "Auction_MailLostLot_Body", "[UNTRANSLATED]Attached items that were mailed and lost have been returned.");
		const char *pchMessageSubj = langTranslateMessageKeyDefault(eaAuctionLots[i]->iLangID, "Auction_MailLostLot_Subject", "[UNTRANSLATED]Recovered Item: {ItemName} {Count > 0 ? (and {Count} more) }");
		NOCONST(Item) *pSubjectItem = NULL;
		const char *pchSubjectItemName = "(no items attached)";

		estrClear(&estrSubject);

		// Attach items
		for (k = 0; k < eaSize(&eaAuctionLots[i]->ppItemsV2); k++)
		{
			eaPush(&pEmailMessage->ppItems, StructCloneNoConst(parse_Item, eaAuctionLots[i]->ppItemsV2[k]->slot.pItem));
		}

		// Generate the message subject based on the first attached item
		pSubjectItem = eaGet(&pEmailMessage->ppItems, 0);
		if (pSubjectItem)
		{
			pchSubjectItemName = item_GetNameLang(CONTAINER_RECONST(Item, pSubjectItem), eaAuctionLots[i]->iLangID, NULL);
		}

		strfmt_FromArgs(&estrSubject, pchMessageSubj,
			STRFMT_STRING("ItemName", pchSubjectItemName),
			STRFMT_INT("Count", eaSize(&pEmailMessage->ppItems) - 1),
			// mostly just hack to ensure that container ids are locked (otherwise bugs), otherwise I don't really expect the message to use it
			STRFMT_INT("ContainerID", eaAuctionLots[i]->iContainerID),
			STRFMT_END);

		// Fill in message details
		pEmailMessage->uSent = uNow;
		pEmailMessage->pchSubject = StructAllocString(estrSubject);
		pEmailMessage->pchBody = StructAllocString(pchMessageBody);
		pEmailMessage->pchSenderName = StructAllocString(pchMessageFrom);
		pEmailMessage->eSenderType = kEmailV3Type_NPC;

		// Post message
		if (EmailV3_trh_DeliverMessage(ATR_PASS_ARGS, pSharedBank, pEmailMessage, ++pSharedBank->pEmailV3->uLastUsedID) != TRANSACTION_OUTCOME_SUCCESS)
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}

		// Close lot
		eaAuctionLots[i]->state = ALS_Closed;
	}

	estrDestroy(&estrSubject);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.bDoneMailAuctionItemsFixup");
enumTransactionOutcome auction_tr_MailLostLots_Complete(ATR_ARGS, NOCONST(Entity) *pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer))
	{
		pEnt->pPlayer->bDoneMailAuctionItemsFixup = true;
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

#endif

#include "AutoGen/AuctionLot_Transact_h_ast.c"