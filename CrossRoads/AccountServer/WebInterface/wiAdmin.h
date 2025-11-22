#pragma once

#define WI_ADMIN_DIR "/admin/"

typedef struct WICWebRequest WICWebRequest;

bool wiHandleAdmin(SA_PARAM_NN_VALID WICWebRequest *pWebRequest);