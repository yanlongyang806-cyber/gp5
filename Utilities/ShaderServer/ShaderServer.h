
typedef struct Checksum Checksum;
typedef struct NetComm NetComm;

void shaderServerWorkerInit(NetComm *comm);
void shaderServerWorkerUpdate(NetComm *comm);

extern Checksum shaderserver_checksum;

// In seconds:
#define PING_TIME 1
#define TIMEOUT_TIME 10