#pragma once
GCC_SYSTEM

typedef struct ExprContext ExprContext;

// Fetches the static context used for generating and evaluating expressions in PowerTreeRespecConfig
ExprContext *powerTreeEval_GetContextRespec(void);
