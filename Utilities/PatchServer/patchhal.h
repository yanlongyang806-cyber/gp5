#ifndef CRYPTIC_PATCHHAL_H
#define CRYPTIC_PATCHHAL_H

/*****************************************************************

Hog Abstraction Layer

The goal of system is to hide the internals of getting hog files 
on a PatchServer. This allows us continue writing to files during 
compaction. While a file is compacting, writes to that file go 
into a temporary file whose name ends .delayedwrite.hogg. When 
compaction is completed, those writes are pushed into the real 
file. 

All attempts to get a HogFile handle should go through 
patchHALGetWriteHandle or patchHALGetReadHandle or one of their
variants. All systems with one of these handles should perform 
their own hogFileDestroy when done. 

*****************************************************************/

#define PATCHSERVER_HOG_DATALIST_JOURNAL_SIZE (32*1024*1024)
#define SMALL_PATCHSERVER_HOG_DATALIST_JOURNAL_SIZE (1024*1024)

typedef struct HogFile HogFile;
typedef struct PatchServerDb PatchServerDb;
typedef struct FileNameAndOldName FileNameAndOldName;

typedef struct HALHogFile
{
	PatchServerDb *serverdb;
	const char *stashTableKey;
	char *filename;
	HogFile *hog;
	bool temp;
} HALHogFile;

HALHogFile *patchHALGetReadHogHandleEx(PatchServerDb *serverdb, U32 checkin_time, FileNameAndOldName *pathInHogg, const char *filename MEM_DBG_PARMS);
#define patchHALGetReadHogHandle(serverdb, checkin_time, pathInHoggs) patchHALGetReadHogHandleEx(serverdb, checkin_time, pathInHoggs, NULL MEM_DBG_PARMS_INIT)
#define patchHALGetReadHogHandleFromFilename(serverdb, checkin_time, filename) patchHALGetReadHogHandleEx(serverdb, checkin_time, NULL, filename MEM_DBG_PARMS_INIT)

HALHogFile* patchHALGetHogHandleEx(PatchServerDb *serverdb, U32 hoggKey, bool createhogg, bool alert, bool write, bool forceTemp, bool forceNoTemp, bool *isTempName MEM_DBG_PARMS);
#define patchHALGetHogHandle(serverdb, hoggKey, createhogg, alert, write, forceTemp, forceNoTemp, isTempName) patchHALGetHogHandleEx(serverdb, hoggKey, createhogg, alert, write, forceTemp, forceNoTemp, isTempName MEM_DBG_PARMS_INIT)
HALHogFile* patchHALGetHogHandleByTime(PatchServerDb *serverdb, U32 checkin_time, bool createhogg, bool alert, bool write, bool forceTemp, bool forceNoTemp, bool *isTempName MEM_DBG_PARMS);

HALHogFile *patchHALGetWriteHogHandleEx(PatchServerDb *serverdb, U32 checkin_time, bool createhogg, bool alert MEM_DBG_PARMS);
#define patchHALGetWriteHogHandle(serverdb, checkin_time, createhogg) patchHALGetWriteHogHandleEx(serverdb, checkin_time, createhogg, true MEM_DBG_PARMS_INIT)
#define patchHALGetWriteHogHandleNoAlert(serverdb, checkin_time, createhogg) patchHALGetWriteHogHandleEx(serverdb, checkin_time, createhogg, false MEM_DBG_PARMS_INIT)

HALHogFile *patchHALHogFileAddRefEx(HALHogFile *halhog MEM_DBG_PARMS);
#define patchHALHogFileAddRef(halhog) patchHALHogFileAddRefEx(halhog MEM_DBG_PARMS_INIT)
void patchHALHogFileDestroy(HALHogFile *halhog, bool freehandle);

// Close a hogg file.
void patchHALCloseHog(PatchServerDb *serverdb, HogFile *hogg, bool temp);

void patchHALCloseAllHogs(PatchServerDb *serverdb);

U32 patchHALGetHogKey(U32 timeStamp);
void patchHALGetHogFileNameFromTimeStamp(char *hoggFileNameOut, size_t hoggFileNameOut_size, const char *dbName, U32 timeStamp);

HogFile *patchHALGetCachedHandle(PatchServerDb *serverdb, int hoggKey, bool temp);

bool patchHALHogFileInUse(HogFile *hogg);

void patchHALHogDelete(PatchServerDb *serverdb, HogFile *hogg, const char *filename);

void patchHALTick(PatchServerDb *serverdb);

#endif  // CRYPTIC_PATCHHAL_H
