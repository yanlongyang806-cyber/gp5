#pragma once


#ifdef __cplusplus
	extern "C" {
#endif

const char *getUserName(void);
const char *getHostName(void);

#define MACHINE_ID_MAX_LEN 256
const char *getMachineID(char *raw32);

#ifdef __cplusplus
}  // extern "C"
#endif
