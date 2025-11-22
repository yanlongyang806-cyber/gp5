#include "CoreCommon.h"
#include "Materials.h"

void OVERRIDE_LATELINK_ProdSpecificGlobalConfigSetup(void)
{
	materialErrorOnMissingFallbacks = false;
}

