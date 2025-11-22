/***************************************************************************



***************************************************************************/

#pragma once

typedef struct CurrencyExchangeGlobalUIData CurrencyExchangeGlobalUIData;

void gslCurrencyExchange_OncePerFrame(void);
void gslCurrencyExchange_SchemaInit(void);
CurrencyExchangeGlobalUIData* gslCurrencyExchange_GetGlobalUIData(void);