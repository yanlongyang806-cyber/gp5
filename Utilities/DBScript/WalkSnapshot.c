#include "WalkSnapshot.h"
#include "hoglib.h"

void WalkSnapshot(GlobalType type, const char *snapshotFilename, const char *offlineFilename, int numThreads, DumpLoadedContainerCallback func)
{
	if(!snapshotFilename[0])
		return;

	objSetContainerSourceToHogFile(snapshotFilename,0,NULL,NULL);
	if(numThreads)
		objSetMultiThreadedLoadThreads(numThreads);

	objSetDumpMode(true);
	objSetDumpType(type);
	objSetDumpLoadedContainerCallback(func);

	objLoadContainersFromHoggForDump(snapshotFilename, gContainerSource.dumpContainerType);
	if(offlineFilename[0])
	{
		objLoadContainersFromOfflineHoggForDumpEx(offlineFilename, gContainerSource.dumpContainerType);
	}

	objCloseContainerSource();
}