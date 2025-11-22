#pragma once

#define WI_ACCOUNTS_DIR "/accounts/"

typedef struct WICWebRequest WICWebRequest;

bool wiHandleAccounts(SA_PARAM_NN_VALID WICWebRequest *pWebRequest);