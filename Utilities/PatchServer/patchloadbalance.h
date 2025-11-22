#ifndef CRYPTIC_PATCHLOADBALANCE_H
#define CRYPTIC_PATCHLOADBALANCE_H

typedef struct PatchClientLink PatchClientLink;

// HTTP information to send to a client
AUTO_STRUCT;
typedef struct ClientHttpInfo
{
	char *info;									// Chosen HTTP info
	bool load_balancer;							// If true, we need to load balance this request.
} ClientHttpInfo;

// Callback for sending set view responses.
typedef void (*patchSendViewStatusCallback)(PatchClientLink *client, bool view_valid, const char *err_msg, ClientHttpInfo *http_info);

// Resolve a load balancer request.
void patchLoadBalanceRequest(PatchClientLink *client, ClientHttpInfo *http_info);

// Fulfill any completed load balancer requests.
void patchLoadBalanceProcess(patchSendViewStatusCallback callback);

#endif  // CRYPTIC_PATCHLOADBALANCE_H
