#pragma once

typedef struct WICWebRequest WICWebRequest;

AUTO_ENUM;
typedef enum WebMessageBoxFlags
{
	WMBF_BackButton				= BIT(0),
	WMBF_Error					= BIT(1),

	WMBF_MAX, EIGNORE
} WebMessageBoxFlags;
#define WebMessageBoxFlags_NUMBITS 3
STATIC_ASSERT(WMBF_MAX == ((1 << (WebMessageBoxFlags_NUMBITS-2))+1));

void wiAppendMessageBox(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
						SA_PARAM_NN_STR const char *pSubject,
						SA_PARAM_NN_STR const char *pMessage,
						WebMessageBoxFlags options);

void accountServerHttpInit(unsigned int port);