/***************************************************************************



***************************************************************************/

#include "gclTrade.h"
#include "cmdparse.h"
#include "GlobalTypes.h"

// The referenced entity has requested a trade session with this entity.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD ACMD_CLIENTONLY;
void trade_ReceiveTradeRequest(bool bSender)
{
	if (bSender || !gConf.bEnableTwoStepTradeRequest)
	{
		globCmdParse("GenSendMessage Trade_Root Show");
	}
	else
	{
		globCmdParse("GenSendMessage TradeRequest_Root Show");
	}
}

// The referenced entity's trade session is being canceled.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD ACMD_CLIENTONLY;
void trade_ReceiveTradeCancel(void)
{
	globCmdParse("GenSendMessage Trade_Root Hide"); // Do not send "Close" as that would cause an infinite loop
	if (gConf.bEnableTwoStepTradeRequest)
	{
		globCmdParse("GenSendMessage TradeRequest_Root Hide");
	}
	if(gConf.bEnableEscrowTrades)
	{
		globCmdParse("GenSendMessage TradeEscrow_Root Hide");
	}
}

// The referenced entity's trade session is now in escrow.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD ACMD_CLIENTONLY;
void trade_ReceiveTradeEscrow(void)
{
	if(gConf.bEnableEscrowTrades)
	{
		globCmdParse("GenSendMessage TradeEscrow_Root Show");
	}
}