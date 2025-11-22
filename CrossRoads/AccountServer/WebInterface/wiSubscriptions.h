#pragma once

#define WI_SUBSCRIPTIONS_DIR "/subscriptions/"

typedef struct WICWebRequest WICWebRequest;

bool wiHandleSubscriptions(SA_PARAM_NN_VALID WICWebRequest *pWebRequest);