/***************************************************************************



***************************************************************************/

#ifndef CLIENTTRADE_H_
#define CLIENTTRADE_H_

#include "GlobalTypeEnum.h"

// Trade client commands
void trade_ReceiveTradeRequest(bool bSender);
void trade_ReceiveTradeCancel(void);
void trade_ReceiveTradeEscrow(void);

#endif
