/***************************************************************************



***************************************************************************/

#include "itemCommon.h"
#include "ResourceManager.h"


typedef struct Item Item;
typedef struct Entity Entity;
typedef struct ParseTable ParseTable;
typedef struct CmdSlowReturnForServerMonitorInfo CmdSlowReturnForServerMonitorInfo;
typedef struct CmdContext CmdContext;

void WebRequestSlow_BuildXMLResponseString(char **responseString, ParseTable *tpi, void *struct_mem);
void WebRequestSlow_BuildXMLResponseStringWithType(char **responseString, char *type, char *val);
void WebRequestSlow_SendXMLRPCReturn(bool success, CmdSlowReturnForServerMonitorInfo *slowReturnInfo);
CmdSlowReturnForServerMonitorInfo *WebRequestSlow_SetupSlowReturn(CmdContext *pContext);
void AccountSharedBankReceived_CB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, Entity *pEnt, void *pUserData);

typedef struct MicroTransactionInfo MicroTransactionInfo;
typedef struct MTUserProduct MTUserProduct;

#define MICROTRANS_USERCATALOG_SUCCESS "success"
#define MICROTRANS_USERCATALOG_FAIL "failure"
#define MICROTRANS_USERCATALOG_PROCESSING "processing"

AUTO_STRUCT;
typedef struct MicroTransactionCatalogRequest
{
	U32 characterID;
	U32 accountID;
} MicroTransactionCatalogRequest;

void gslAPProductListUpdateCache(void);
MicroTransactionInfo *GetMicrotransactionProducts(MicroTransactionCatalogRequest *request);
bool HasProductCatalogChanged(U32 uLastTimeUpdated);

AUTO_STRUCT;
typedef struct MicroTransactionUserCatalogResponse
{
	char *result_string;
	char *error;
	EARRAY_OF(MTUserProduct) ppProducts;
} MicroTransactionUserCatalogResponse;

AUTO_STRUCT;
typedef struct MicroTransactionUserCatalogRequest
{
	U32 characterID;
	U32 accountID;
} MicroTransactionUserCatalogRequest;

MicroTransactionUserCatalogResponse *GetMicrotransactionCatalogForCharacter(MicroTransactionUserCatalogRequest *request);

AUTO_STRUCT;
typedef struct MicroTransactionUserPurchaseResponse
{
	int request_id;
	char *result_string;
	char *error;
} MicroTransactionUserPurchaseResponse;

AUTO_STRUCT;
typedef struct MicroTransactionUserPurchaseRequest
{
	U32 characterID;
	U32 accountID;
	U32 productID;
	int expectedPrice;  AST(DEFAULT(-1))
} MicroTransactionUserPurchaseRequest;

MicroTransactionUserPurchaseResponse *MicrotransactionProductPurchase(MicroTransactionUserPurchaseRequest *request);

AUTO_STRUCT;
typedef struct MicroTransactionPurchaseStatusRequest
{
	U32 accountID;
	U32 requestID;
} MicroTransactionPurchaseStatusRequest;

MicroTransactionUserPurchaseResponse *MicrotransactionProductPurchaseStatus(MicroTransactionPurchaseStatusRequest *request);


AUTO_STRUCT;
typedef struct NPCMailRequest
{
	U32 characterID;
	char *fromName;
	char *subject;
	char *body;
	char *itemDefName;
	U32 itemQuantity;
	CmdSlowReturnForServerMonitorInfo* pSlowReturnInfo; NO_AST
} NPCMailRequest;

void SendNPCMailWebRequest_cb(TransactionReturnVal* val, NPCMailRequest* request);