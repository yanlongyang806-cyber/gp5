#include "stdtypes.h"

void aptMimicProxyMain(void);

AUTO_ENUM;
typedef enum AccountProxyMimicTest
{
	apMimic_None = 0,

	apMimic_GetAccountData,

	apMimic_SimpleSet,
	apMimic_SimpleSetScoped,
	apMimic_SimpleIncrement,

	apMimic_KeySet,
	apMimic_KeySetScoped,
	apMimic_KeyChange,

	apMimic_Purchase,

	apMimic_Count,
} AccountProxyMimicTest;