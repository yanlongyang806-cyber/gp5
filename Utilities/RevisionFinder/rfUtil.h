
#define RF_MAX_ASYNCS 9999 //Won't have that many things running, just loops the IDs over this range

typedef struct NameValuePair NameValuePair;
typedef struct Deployment Deployment;
typedef struct QueryableProcessHandle QueryableProcessHandle;

//Check a list of NameValuePairs for an entry with a certain key
int GetVarIndex(const NameValuePair **ppVarList, const char *pKey);

int StrEqual(const char *stringA, const char *stringB);

int SameDeployType(const Deployment *deploymentA, const Deployment *deploymentB);

bool CheckListForInt(const U32* theList, U32 target);

typedef void (*SystemCallbackFunc)(int errorLevel, const char* pOutput);

typedef struct SysAsyncRequest {
	U32 id;
	SystemCallbackFunc callback;
	QueryableProcessHandle *handle;
} SysAsyncRequest;

void rfSystemAsync(const char* cmd, SystemCallbackFunc callback);
void rfSystemAsyncTick();