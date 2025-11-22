#include "../../3rdparty/zlib/zlib.h"
#include "Alerts.h"
#include "earray.h"
#include "error.h"
#include "estring.h"
#include "file.h"
#include "FilespecMap.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "hoglib.h"
#include "logging.h"
#include "mathutil.h"
#include "patchcommonutils.h"
#include "patcher_comm.h"
#include "patchfile.h"
#include "patchdb_h_ast.h"
#include "patchjournal.h"
#include "patchproject.h"
#include "patchpruning.h"
#include "patchserver.h"
#include "patchserverdb.h"
#include "patchserverdb_h_ast.h"
#include "pcl_typedefs.h"
#include "piglib.h"
#include "piglib_internal.h"
#include "qsortG.h"
#include "StashTable.h"
#include "stringutil.h"
#include "sysutil.h"
#include "textparser.h"
#include "TimedCallback.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "utils.h"
#include "wininclude.h"
#include "zutils.h"
#include "patchhal.h"
#include "patchcompaction.h"

static bool g_verifyHoggs_fix_hoggs;
static bool g_verifyHoggs_load_data;

void patchserverdbNameInHogg(	PatchServerDb* serverdb,
								FileVersion* ver,
								FileNameAndOldName* nameOut)
{
	// Make a name that makes sense.

	sprintf_s(	SAFESTR(nameOut->name),
				"%s%s/r%u_t%u_b%u%s%s",
				serverdb->persist_prefix,
				ver->parent->path,
				ver->checkin->rev,
				ver->checkin->time,
				ver->checkin->branch,
				NULL_TO_EMPTY(ver->checkin->sandbox),
				ver->flags & FILEVERSION_DELETED ? "_del" : "");
				
	// Make a crappy old name that sucks.

	sprintf_s(	SAFESTR(nameOut->oldName),
				"%s%s/%u_%d%s_%d%s",
				serverdb->persist_prefix,
				ver->parent->path,
				ver->checkin->time,
				ver->checkin->branch,
				NULL_TO_EMPTY(ver->checkin->sandbox),
				ver->version,
				ver->flags & FILEVERSION_DELETED ? "_del" : "");
}

static void clearFilesFromDir(DirEntry *dir_entry, void *user_data)
{
	int i;
	for(i = 0; i < eaSize(&dir_entry->versions); i++)
	{
		PatchFile * patch = dir_entry->versions[i]->patch;
		patchfileDestroy(&patch);
		SAFE_FREE(dir_entry->versions[i]->header_data);
	}
}

void patchserverdbUnloadFiles(PatchServerDb *serverdb)
{
	if(serverdb && serverdb->db)
		patchForEachDirEntry(serverdb->db, clearFilesFromDir, NULL);
}

int cmpIdx(const int * x, const int * y)
{
	return *y - *x;
}

// !!!: If anyone ever tries to write something like this ever again, I will hunt you down and find you, and I will not be kind. <NPK 2009-06-15>
//void indexCheckins(PatchDB * db)
//{
//	int i, j;
//
//	for(i = 0; i < eaSize(&db->checkins); i++)
//	{
//		Checkin * checkin = db->checkins[i];
//		checkin->rev = i;
//		for(j = 0; j < eaSize(&checkin->versions); j++)
//			checkin->versions[j]->rev = checkin->rev;
//	}
//}

static void clearCheckinFromDir(DirEntry *dir, void *userdata)
{
	int i, rev = PTR_TO_U32(userdata);
	for(i = eaSize(&dir->versions)-1; i >= 0; i--)
		if(dir->versions[i]->rev == rev)
			fileVersionDestroy(eaRemove(&dir->versions, i));
}

//bool checkForDuplicateCheckins(PatchServerDb *serverdb)
//{
//	int i, j;
//	int * remove_list = NULL;
//	bool ret = true;
//	PatchDB *db = serverdb->db;
//
//	if(!db)
//		return ret;
//
//	for(i = 0; i < eaSize(&db->checkins); i++)
//	{
//		for(j = i + 1; j < eaSize(&db->checkins); j++)
//		{
//			Checkin * ci = db->checkins[i];
//			Checkin * cj = db->checkins[j];
//
//			if(	!stricmp(NULL_TO_EMPTY(ci->author), NULL_TO_EMPTY(cj->author)) &&
//				ci->branch == cj->branch &&
//				!stricmp(NULL_TO_EMPTY(ci->comment), NULL_TO_EMPTY(cj->comment)) &&
//				!stricmp(NULL_TO_EMPTY(ci->sandbox), NULL_TO_EMPTY(cj->sandbox)) &&
//				ci->time == cj->time)
//			{
//				eaiPushUnique(&remove_list, j);
//				ret = false;
//			}
//		}
//	}
//
//	ea32QSort(remove_list, cmpIdx);
//
//	for(i = 0; i < eaiSize(&remove_list); i++)
//	{
//		int rev = db->checkins[remove_list[i]]->rev;
//
//		printf("Duplicate checkin found: rev=%i\n", rev);
//		log_printf(LOG_PATCHSERVER, "Duplicate checkin found: rev=%i", rev);
//
//		checkinFree(&(db->checkins[remove_list[i]]));
//		eaRemove(&db->checkins, remove_list[i]);
//
//		patchForEachDirEntry(db, clearCheckinFromDir, U32_TO_PTR(rev));
//
//		indexCheckins(db);
//	}
//
//	eaiDestroy(&remove_list);
//
//	return ret;
//}

bool updateHeadersInDir(PatchServerDb *serverdb, DirEntry * dir_entry)
{
	int i;
	bool ret = true;

	if(eaSize(&dir_entry->versions) > 0 && pigShouldCacheHeaderData(strrchr(dir_entry->name, '.')))
	{
		for(i = 0; i < eaSize(&dir_entry->versions); i++)
		{
			FileVersion*		ver = dir_entry->versions[i];
			NewPigEntry			pe;
			HALHogFile*			halhog;
			HogFileIndex		hfi;
			U32					header_size;
			U8*					header_data;
			FileNameAndOldName	pathInHogg;

			patchserverdbNameInHogg(serverdb, ver, &pathInHogg);

			halhog = patchHALGetReadHogHandle(serverdb, ver->checkin->time, &pathInHogg);
			
			if(!halhog)
			{
				continue;
			}
			
			hfi = hogFileFind(halhog->hog, pathInHogg.name);
			if(hfi == HOG_INVALID_INDEX)
			{
				hfi = hogFileFind(halhog->hog, pathInHogg.oldName);

				if(hfi == HOG_INVALID_INDEX)
				{
					patchHALHogFileDestroy(halhog, false);
					continue;
				}
			}

			ZeroStruct(&pe);
			pe.data = hogFileExtract(halhog->hog, hfi, &pe.size, NULL);
			pe.fname = ver->parent->path;
			header_data = pigGetHeaderData(&pe, &header_size);
			ver->header_data = NULL;
			if(header_data)
			{
				U32 new_checksum = patchChecksum(header_data, header_size);

				if(ver->header_size != header_size || ver->header_checksum != new_checksum)
					ret = false;

				if(ver->header_size != 0 && ver->header_size != header_size)
					printf("Invalid size %u was stored for header %s\n", ver->header_size, ver->parent->path);

				if(ver->header_size != 0 && ver->header_checksum != new_checksum)
					printf("Invalid checksum %u was stored for header %s\n", ver->header_checksum, ver->parent->path);

				ver->header_size = header_size;
				ver->header_checksum = new_checksum;
			}
			else
			{
				ver->header_size = 0;
				ver->header_checksum = 0;
			}
			patchHALHogFileDestroy(halhog, false);
			SAFE_FREE(pe.data);
		}
	}

	for(i = 0; i < eaSize(&dir_entry->children); i++)
	{
		if(!updateHeadersInDir(serverdb, dir_entry->children[i]))
			ret = false;
	}

	return ret;
}

typedef struct FileInHogg {
	U32			checkinTime;
	const char*	reason;
	char*		pathInHogg;
} FileInHogg;

typedef struct VerifyHoggsInDirInfo {
	PatchServerDb*		serverdb;
	time_t				startTime;
	
	U64					dbTotalFileVersionCount;
	U64					dbTotalFileBytes;

	struct {
		U64				fileVersionsCheckedCount;
		U64				fileBytesChecked;

		FileVersion**	versToRemove;
		
		FileInHogg**	filesToRemoveFromHoggs;
	} out;
} VerifyHoggsInDirInfo;

static void addFileToRemoveFromHogg(FileInHogg*** fihs,
									U32 checkinTime,
									const char* pathInHogg,
									const char* reason)
{
	FileInHogg* fih = callocStruct(FileInHogg);
	fih->checkinTime = checkinTime;
	fih->reason = reason;
	fih->pathInHogg = strdup(pathInHogg);
	eaPush(fihs, fih);
}

static void verifyHoggsInDirUpdateTitleBar(	DirEntry* dir,
											FileVersion* ver,
											S32 verIndex,
											VerifyHoggsInDirInfo* vhidInfo)
{
	U64			fileVersionsCheckedCount = ++vhidInfo->out.fileVersionsCheckedCount;
	const U64	previousBytes = vhidInfo->out.fileBytesChecked;
	U64			fileBytesChecked = vhidInfo->out.fileBytesChecked += ver->size;
	static U32	lastUpdateTime;
	
	if(	fileBytesChecked == vhidInfo->dbTotalFileBytes
		||
		(U32)(timerCpuTicks() - lastUpdateTime) > timerCpuSpeed() * 0.1f &&
		previousBytes >> 20 != fileBytesChecked >> 20)
	{
		char	buffer[MAX_PATH];
		U32		totalTime = time(NULL) - vhidInfo->startTime;
		
		lastUpdateTime = timerCpuTicks();
		
		sprintf(buffer,
				"Verifying hoggs (%5.2f%%):"
				" %d:%2.2d:%2.2d,"
				" %s/%s files,"
				" %s/%s MB,"
				" Current: (ver %d/%d) %s"
				,
				g_verifyHoggs_load_data?(vhidInfo->dbTotalFileBytes ?
					(fileBytesChecked * 100.f / vhidInfo->dbTotalFileBytes) :
					0) :
					(vhidInfo->dbTotalFileVersionCount?
						(fileVersionsCheckedCount * 100.f / vhidInfo->dbTotalFileVersionCount) :
						0),
				totalTime / 3600,
				(totalTime / 60) % 60,
				totalTime % 60,
				getCommaSeparatedInt(fileVersionsCheckedCount),
				getCommaSeparatedInt(vhidInfo->dbTotalFileVersionCount),
				getCommaSeparatedInt(fileBytesChecked >> 20),
				getCommaSeparatedInt(vhidInfo->dbTotalFileBytes >> 20),
				verIndex + 1,
				eaSize(&dir->versions),
				dir->path);
				
		setConsoleTitleWithPid(buffer);
	}
}

static bool verifyHoggsInDir(	DirEntry *dir,
								VerifyHoggsInDirInfo* vhidInfo)
{
	bool ret = true;

	EARRAY_FOREACH_REVERSE_BEGIN(dir->versions, i);
		FileVersion*		ver = dir->versions[i];
		FileNameAndOldName	pathInHogg;
		const char*			usedPathInHogg;
		const char*			removeReason = NULL;
		HALHogFile*			halhog;
		HogFileIndex		hfi;
		
		verifyHoggsInDirUpdateTitleBar(dir, ver, i, vhidInfo);

		patchserverdbNameInHogg(vhidInfo->serverdb, ver, &pathInHogg);
		halhog = patchHALGetWriteHogHandle(vhidInfo->serverdb, ver->checkin->time, false);
		usedPathInHogg = pathInHogg.name;
		
		if(!halhog)
		{
			printf("Hogg not found for file: %s\n", usedPathInHogg);
			log_printf(LOG_PATCHSERVER_GENERAL, "Hogg not found for file: %s", usedPathInHogg);
			removeReason = "Hogg file doesn't exist for FileVersion.";
		}
		else
		{
			hfi = hogFileFind(halhog->hog, usedPathInHogg);
			
			if(hfi == HOG_INVALID_INDEX)
			{
				usedPathInHogg = pathInHogg.oldName;
				hfi = hogFileFind(halhog->hog, usedPathInHogg);
			}
			else
			{
				if(g_verifyHoggs_fix_hoggs)
				{
					if(hogFileFind(halhog->hog, pathInHogg.oldName) != HOG_INVALID_INDEX)
					{
						addFileToRemoveFromHogg(&vhidInfo->out.filesToRemoveFromHoggs,
												ver->checkin->time,
												pathInHogg.oldName,
												"New-name-format file exists.");
					}
				}
			}
			
			if(hfi == HOG_INVALID_INDEX)
			{
				if(ver->flags & FILEVERSION_DELETED)
				{
					filelog_printf("patchserver_missing_del", "File not found in hogg: %s", pathInHogg.name);
					log_printf(LOG_PATCHSERVER_GENERAL, "File not found in hogg: %s", pathInHogg.name);
					hogFileModifyUpdateNamed(halhog->hog, pathInHogg.name, malloc(0), 0, ver->modified, NULL);

					if(g_verifyHoggs_fix_hoggs)
					{
						if(hogFileFind(halhog->hog, pathInHogg.oldName) != HOG_INVALID_INDEX)
						{
							addFileToRemoveFromHogg(&vhidInfo->out.filesToRemoveFromHoggs,
													ver->checkin->time,
													pathInHogg.oldName,
													"Added new-name-format \"deleted\" file.");
						}
					}					
				}
				else
				{
					printf("File not found in hogg: %s\n", pathInHogg.name);
					log_printf(LOG_PATCHSERVER_GENERAL, "File not found in hogg: %s", pathInHogg.name);
					removeReason = "File not found in hogg.";
				}
				ret = false;
			}
			else if(hogFileGetFileSize(halhog->hog, hfi) != ver->size)
			{
				printf("File size does not match: %s\n", usedPathInHogg);
				
				log_printf(LOG_PATCHSERVER_GENERAL, "File size does not match: %s", usedPathInHogg);
									
				if(ver->flags & FILEVERSION_DELETED)
				{
					if(g_verifyHoggs_fix_hoggs)
					{
						hogFileModifyUpdateNamed(halhog->hog, pathInHogg.name, malloc(0), 0, ver->modified, NULL);

						if(hogFileFind(halhog->hog, pathInHogg.oldName) != HOG_INVALID_INDEX)
						{
							addFileToRemoveFromHogg(&vhidInfo->out.filesToRemoveFromHoggs,
													ver->checkin->time,
													pathInHogg.oldName,
													"Added new-format-name delete file.");
						}
					}
				}
				else
				{
					removeReason = "File in hogg was the wrong size.";
				}
				ret = false;
			}
			else if(ver->size > 0 && hogFileGetFileChecksum(halhog->hog, hfi) != ver->checksum)
			{
				printf(	"File checksum does not match: %s in hogg %s (%u vs. %u)\n",
						usedPathInHogg,
						hogFileGetArchiveFileName(halhog->hog),
						hogFileGetFileChecksum(halhog->hog, hfi),
						ver->checksum);
						
				log_printf(LOG_PATCHSERVER_GENERAL,
								"File checksum does not match: %s in hogg %s (%u vs. %u)",
								usedPathInHogg,
								hogFileGetArchiveFileName(halhog->hog),
								hogFileGetFileChecksum(halhog->hog, hfi),
								ver->checksum);
								
				removeReason = "Checksum of file in hogg is wrong.";
				ret = false;
			}
			else
			{
				if (g_verifyHoggs_load_data)
				{
					U8*		hog_data;
					U32		hog_size;
					bool	checksum_valid;
					
					hog_data = hogFileExtract(halhog->hog, hfi, &hog_size, &checksum_valid);
					if(!checksum_valid)
					{
						printf(	"There is an invalid checksum for file %s in hogg %s\n",
								usedPathInHogg,
								hogFileGetArchiveFileName(halhog->hog));
								
						log_printf(LOG_PATCHSERVER_GENERAL,
										"There is an invalid checksum for file %s in hogg %s",
										usedPathInHogg,
										hogFileGetArchiveFileName(halhog->hog));
						
						removeReason = "Extracted file from hogg failed the checksum.";
						ret = false;
					}
					SAFE_FREE(hog_data);
				}
			}
		}

		if(removeReason)
		{
			eaPush(&vhidInfo->out.versToRemove, ver);
			
			if(halhog && g_verifyHoggs_fix_hoggs)
			{
				if(hogFileFind(halhog->hog, usedPathInHogg) != HOG_INVALID_INDEX)
				{
					addFileToRemoveFromHogg(&vhidInfo->out.filesToRemoveFromHoggs,
											ver->checkin->time,
											usedPathInHogg,
											removeReason);
				}
			}
		}
		patchHALHogFileDestroy(halhog, false);
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(dir->children, i, isize);
		if(!verifyHoggsInDir(	dir->children[i],
								vhidInfo))
		{
			ret = false;
		}
	EARRAY_FOREACH_END;

	return ret;
}

typedef struct HogCheckInManifestData {
	PatchServerDb*	serverdb;
	bool			scan_ret;
	
	U32				hoggBaseTimeStamp;
	
	FileInHogg**	deletes;
} HogCheckInManifestData;

static bool hogCheckInManifest(	HogFile *handle,
								HogFileIndex index_UNUSED,
								const char* pathInHogg,
								HogCheckInManifestData* hcimData)
{
	char	path[MAX_PATH];
	char	sandbox[MAX_PATH];
	char*	slashBeforeRevisionFileName;
	U32		time;
	int		branch;
	int		version;
	int		rev;

	PERFINFO_AUTO_START_FUNC();

	strcpy(path, pathInHogg);
	forwardSlashes(path);
	slashBeforeRevisionFileName = strrchr(path, '/');
	if(slashBeforeRevisionFileName)
	{
		DirEntry*	dir_entry;
		const char*	pathInDb;

		slashBeforeRevisionFileName[0] = '\0';
		
		// Chop off the persist prefix.
		
		if(	hcimData->serverdb->persist_prefix &&
			strStartsWith(path, hcimData->serverdb->persist_prefix))
		{
			pathInDb = path + strlen(hcimData->serverdb->persist_prefix);
		}else{
			pathInDb = path;
		}

		dir_entry = patchFindPath(hcimData->serverdb->db, pathInDb, 0);

		if(dir_entry)
		{
			FileVersion*	ver;
			const char*		revFileName = slashBeforeRevisionFileName + 1;
			
			sandbox[0] = 0;
			
			if(revFileName[0] == 'r')
			{
				sscanf_s(revFileName, "r%u_t%u_b%u%s", &rev, &time, &branch, SAFESTR(sandbox));
				ver = patchFindVersionInDir(dir_entry, branch, sandbox, rev, PATCHREVISION_NONE); // ignore incremental
				version = SAFE_MEMBER(ver, version);
			}
			else
			{
				sscanf_s(revFileName, "%u_%d%s_%d", &time, &branch, SAFESTR(sandbox), &version);
				rev = patchFindRevByTime(hcimData->serverdb->db, time, branch, sandbox, INT_MAX);
				ver = patchFindVersionInDir(dir_entry, branch, sandbox, rev, PATCHREVISION_NONE); // ignore incremental
			}
			
			if(	!ver ||
				ver->checkin->time != time)
			{
				hcimData->scan_ret = false;
				printf("No version found for file %s\n", pathInHogg);
				log_printf(LOG_PATCHSERVER_GENERAL, "No version found for file %s", pathInHogg);
				addFileToRemoveFromHogg(&hcimData->deletes,
										hcimData->hoggBaseTimeStamp,
										pathInHogg,
										"No matching version found.");
			}
		}
		else
		{
			hcimData->scan_ret = false;
			printf("No manifest entry found for file: %s\n", pathInHogg);
			log_printf(LOG_PATCHSERVER_GENERAL, "No manifest entry found for file: %s", pathInHogg);
			addFileToRemoveFromHogg(&hcimData->deletes,
									hcimData->hoggBaseTimeStamp,
									pathInHogg,
									"No manifest entry for this path.");
		}
	}
	else
	{
		hcimData->scan_ret = false;
		printf("Found a file without any slashes: %s\n", pathInHogg);
		log_printf(LOG_PATCHSERVER_GENERAL, "Found a file without any slashes: %s", pathInHogg);
		addFileToRemoveFromHogg(&hcimData->deletes,
								hcimData->hoggBaseTimeStamp,
								pathInHogg,
								"Not a valid version file (no slashes).");
	}

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}

static FileScanAction verifyHoggDir(const char* dir,
									struct _finddata32_t* data,
									HogCheckInManifestData* hcimData)
{
	if(strEndsWith(data->name, ".hogg"))
	{
		U32		baseTimeStamp = atoi(data->name);
		char	fullPath[MAX_PATH];
		char	checkPath[MAX_PATH];
		
		patchHALGetHogFileNameFromTimeStamp(	SAFESTR(checkPath),
										hcimData->serverdb->name,
										baseTimeStamp);
		
		sprintf(fullPath, "%s/%s", dir, data->name);
		
		if(stricmp(fullPath, checkPath)){
			printf("Skipping malformed hogg file name: %s\n", fullPath);
		}else{
			HogFile * hogg;
			int error;
			
			hcimData->hoggBaseTimeStamp = baseTimeStamp;
			
			hogg = hogFileRead(fullPath, NULL, PIGERR_PRINTF, &error, HOG_NOCREATE);
			if(hogg && !error)
			{
				hogScanAllFiles(hogg, hogCheckInManifest, hcimData);
			}
			if(hogg)
			{
				hogFileDestroy(hogg, true);
			}
		}
	}

	return FSA_EXPLORE_DIRECTORY;
}

static void countFileVersions(	DirEntry* dir,
								U64* countOut,
								U64* totalBytesOut)
{
	U64 count = eaSize(&dir->versions);
	U64 totalBytes = 0;
	
	EARRAY_CONST_FOREACH_BEGIN(dir->versions, i, isize);
		FileVersion* v = dir->versions[i];
		
		totalBytes += v->size;
	EARRAY_FOREACH_END;
	
	EARRAY_CONST_FOREACH_BEGIN(dir->children, i, isize);
		U64 childCount;
		U64 childBytes;
		
		countFileVersions(	dir->children[i],
							&childCount,
							&childBytes);
							
		count += childCount;
		totalBytes += childBytes;
	EARRAY_FOREACH_END;
	
	*countOut = count;
	*totalBytesOut = totalBytes;
}

static S32 compareFileInHoggByReason(	const FileInHogg** fih1,
										const FileInHogg** fih2)
{
	S32 ret = strcmp(	NULL_TO_EMPTY(fih1[0]->reason),
						NULL_TO_EMPTY(fih2[0]->reason));
	
	if(!ret){
		ret = strcmp(fih1[0]->pathInHogg, fih2[0]->pathInHogg);
	}
	
	return ret;
}

static void processFilesToRemoveFromHoggs(	PatchServerDb* serverdb,
											FileInHogg*** fihsPtr)
{
	if(eaSize(fihsPtr)){
		FileInHogg**	fihs = *fihsPtr;
		char			logFilePath[MAX_PATH];
		FILE*			f;
		
		eaQSort(fihs, compareFileInHoggByReason);
		
		*fihsPtr = NULL;
		
		sprintf(logFilePath, "./%s.FilesToRemoveFromHoggs.%"FORM_LL"d.txt", serverdb->name, time(NULL));
		f = fopen(logFilePath, "wt");
		
		assertmsgf(f, "Can't open file for writing: %s", logFilePath);
		
		fprintf(f,
				"\nATTENTION, FOLLOW THESE STEPS:\n"
				"  1. Read this file for erroneously added files.  It should only contain files that should definitely be deleted from the hoggs.\n"
				"  2. Close Notepad.  A prompt will appear.\n"
				"  3. Choose whether to delete these files.\n\n"
				"Project: %s\n\n",
				serverdb->name);
		
		EARRAY_CONST_FOREACH_BEGIN(fihs, i, isize);
			FileInHogg* fih = fihs[i];
			
			if(	!i ||
				strcmp(NULL_TO_EMPTY(fih->reason), NULL_TO_EMPTY(fihs[i-1]->reason)))
			{
				fprintf(f, "\n\nThe files will be deleted because: %s\n\n", fih->reason);
			}			
			
			fprintf(f, "%s\n", fih->pathInHogg);
		EARRAY_FOREACH_END;
		
		fclose(f);
		
		{
			char cmd[MAX_PATH];
			
			sprintf(cmd, "notepad.exe \"%s\"", logFilePath);
			
			while(1){
				S32 msgResult;
				
				system(cmd);
				
				msgResult = MessageBox(	NULL,
										L"Do you approve deleting those files?\n\n"
										L"YES: Delete the files from the hoggs\n"
										L"NO: Don't delete the files\n"
										L"CANCEL: Open the file list again",
										L"APPROVE ME!",
										MB_YESNOCANCEL | MB_ICONQUESTION);
				
				if(msgResult == IDNO){
					printf("Not deleting files from hoggs!\n");
					break;
				}
				if(msgResult == IDYES){
					msgResult = MessageBox(	NULL,
											L"Are you sure?\n\n"
											L"YES: Delete the files from the hoggs (final answer)\n"
											L"NO: Open the file list again",
											L"ARE YOU SURE?",
											MB_YESNO | MB_ICONQUESTION);
					
					if(msgResult == IDYES){
						printf("Deleting files in hoggs:\n");
						
						EARRAY_CONST_FOREACH_BEGIN(fihs, i, isize);
							FileInHogg* fih = fihs[i];
							HALHogFile*	halhog;
							
							printf("  %s\n", fih->pathInHogg);
							
							halhog = patchHALGetWriteHogHandle(serverdb, fih->checkinTime, false);
							
							if(!halhog){
								continue;
							}
							
							patchserverdbHogDelete(serverdb, halhog->hog, fih->pathInHogg, "VerifyRemove");
							patchHALHogFileDestroy(halhog, false);
						EARRAY_FOREACH_END;
						
						printf("Done deleting files in hoggs.\n");

						patchHALCloseAllHogs(serverdb);
						
						break;
					}
				}
			}
		}
	}
}

static void verifyHoggs(PatchServerDb *serverdb, bool fix_hoggs, bool verify_hoggs_load_data, bool fatalerror_on_verify_failure)
{
	bool			ret = true;
	char			manifest_name[MAX_PATH];
	FileInHogg**	filesToRemoveFromHoggs = NULL;

	g_verifyHoggs_fix_hoggs = fix_hoggs;
	g_verifyHoggs_load_data = verify_hoggs_load_data;

	loadstart_printf("Verifying Hoggs...");
	log_printf(LOG_PATCHSERVER_GENERAL, "Checking %s...", serverdb->name);
	
	// Check all the files in the manifest to see if they need to be removed from the manifest.
	{
		VerifyHoggsInDirInfo vhidInfo = {0};
		
		vhidInfo.serverdb = serverdb;
		vhidInfo.startTime = time(NULL);
		
		countFileVersions(	&serverdb->db->root,
							&vhidInfo.dbTotalFileVersionCount,
							&vhidInfo.dbTotalFileBytes);
							
		if(!verifyHoggsInDir(	&serverdb->db->root,
								&vhidInfo))
		{
			ret = false;
		}
		
		if(vhidInfo.out.versToRemove)
		{
			printf("Removing %d versions from db...", eaSize(&vhidInfo.out.versToRemove));
			
			EARRAY_CONST_FOREACH_BEGIN(vhidInfo.out.versToRemove, i, isize);
				fileVersionRemoveAndDestroy(serverdb->db, vhidInfo.out.versToRemove[i]);
			EARRAY_FOREACH_END;
			
			eaDestroy(&vhidInfo.out.versToRemove);
			
			printf("done.\n");
		}
		
		eaPushEArray(&filesToRemoveFromHoggs, &vhidInfo.out.filesToRemoveFromHoggs);
	}

	// Check all the hogg files to see if they contain anything that isn't in the manifest.
	
	if(fix_hoggs){
		HogCheckInManifestData	hcimData = {0};
		char					path[MAX_PATH];
		
		hcimData.serverdb = serverdb;
		hcimData.scan_ret = true;

		sprintf(path, "./%s", serverdb->name);
		fileScanAllDataDirs(path, verifyHoggDir, &hcimData);
		if(!hcimData.scan_ret)
		{
			ret = false;
		}

		eaPushEArray(&filesToRemoveFromHoggs, &hcimData.deletes);
	}

	//if(!checkForDuplicateCheckins(serverdb))
	//{
	//	ret = false;
	//}
	if(verify_hoggs_load_data)
	{
		if(!updateHeadersInDir(serverdb, &serverdb->db->root))
		{
			ret = false;
		}
	}
	
	processFilesToRemoveFromHoggs(serverdb, &filesToRemoveFromHoggs);

	if(ret)
	{
		printf("Verification successful for %s (you might want to turn off verification with \"-verify 0\")\n", serverdb->name);
	}
	else
	{
		sprintf(manifest_name, "./%s.manifest", serverdb->name);

		printf("Verification failed for %s\n", serverdb->name);
		if (fatalerror_on_verify_failure)
			FatalErrorf("Verification failed for %s", serverdb->name);
		if(fix_hoggs)
			printf("  Attempted to fix files during verification\n");

		loadstart_printf("Closing/flushing hoggs and reloading...");
		patchDbWrite(manifest_name, NULL, serverdb->db);
		patchDbDestroy(&serverdb->db);
		serverdb->db = patchLoadDb(manifest_name, PATCHDB_POOLED_PATHS, serverdb->frozenmap);
		assert(serverdb->db);
		serverdb->db = patchLinkDb(serverdb->db, false);
		loadend_printf("done.");
	}
	loadend_printf("");
}


bool patchserverdbAddViewName(	PatchServerDb *serverdb,
								const char* view_name,
								int branch,
								const char* sandbox,
								int rev,
								const char* comment,
								U32 expires,
								char* err_msg,
								int err_msg_size)
{
	NamedView *view = patchAddNamedView(serverdb->db,
										view_name,
										branch,
										sandbox,
										rev,
										comment,
										expires, 
										err_msg, 
										err_msg_size);

	if(!view)
		return false;

	journalAddNameFlush(eaSize(&serverdb->db->checkins) - 1,
						serverdb->name,
						view_name,
						sandbox,
						branch,
						rev,
						comment,
						expires);
	return true;
}

bool patchserverdbSetExpiration(PatchServerDb *serverdb, const char *view_name, U32 expires, char *msg, int msg_size)
{
	NamedView *view = patchFindNamedView(serverdb->db, view_name);
	if(!view)
	{
		sprintf_s(SAFESTR2(msg), "Could not find view %s.", view_name);
		return false;
	}

	if(view->expires && view->expires <= getCurrentFileTime())
	{
		sprintf_s(SAFESTR2(msg), "View %s has already expired.", view->name);
		//return false;
	}

	msg[0] = '\0';
	view->expires = expires;
	view->dirty = true;

	journalAddExpiresFlush(	eaSize(&serverdb->db->checkins) - 1,
							serverdb->name,
							view_name,
							expires);
	//journalAddViewDirtyFlush(	eaSize(&serverdb->db->checkins) - 1,
	//	serverdb->name,
	//	view_name);
	return true;
}

struct SetFileExpirationData
{
	U32 expires;
	PatchJournal *journal;
};

// Expire all versions of the file.
static void setFileExpiresCB(DirEntry *dir, void *userdata)
{
	struct SetFileExpirationData *data = userdata;
	EARRAY_CONST_FOREACH_BEGIN(dir->versions, i, n);
	{
		FileVersion *ver = dir->versions[i];
		ver->expires = data->expires;
		journalAddFileExpires(data->journal, dir->path, ver->rev, data->expires);
	}
	EARRAY_FOREACH_END;
}

bool patchserverdbSetFileExpiration(PatchServerDb *serverdb, const char *path, U32 expires)
{
	DirEntry *dir;
	struct SetFileExpirationData data;

	PERFINFO_AUTO_START_FUNC();

	// Validate path.
	dir = patchFindPath(serverdb->db, path, false);
	if (!dir)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// Create journal.
	data.journal = journalCreate(eaSize(&serverdb->db->checkins) - 1);

	// Set expires for all children's versions.
	data.expires = expires;
	patchForEachDirEntryPrefix(serverdb->db, path, setFileExpiresCB, &data);

	// Flush journal.
	journalFlushAndDestroy(&data.journal, serverdb->name);

	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

bool patchserverdbIsFileRevisioned(PatchServerDb *serverdb, const char *filename)
{
	int keep_vers;
	if(!serverdb->keepvers) return true;
	return !filespecMapGetInt(serverdb->keepvers, filename, &keep_vers) || keep_vers;
}

StaticDefineInt parse_FilemapConfig_value[] =
{
	DEFINE_INT
	{ "Exclude",	PATCHSERVER_KEEP_EXCLUDE },
	{ "Unlimited",	PATCHSERVER_KEEP_UNLIMITED },
	{ "Bins",		PATCHSERVER_KEEP_BINS },
	DEFINE_END
};

StaticDefineInt parse_PatchBranch_parent_branch[] =
{
	DEFINE_INT
	{ "Tip",	PATCHBRANCH_TIP },
	{ "None",	PATCHBRANCH_NONE },
	DEFINE_END
};

FilespecMap* patchserverdbCreateFilemapFromConfig(FilemapConfig *config)
{
	int i;
	FilespecMap *filemap = filespecMapCreateNoSorting();
	for(i = 0; i < eaSize(&config->files); i++)
	{
		devassert(config->files[i]->value != -2);
		filespecMapAddInt(filemap, config->files[i]->spec, config->files[i]->value);
	}
	return filemap;
}

// How many seconds to wait between patchHALTicks
static U32 gPatchHalTickDelay = 1;
AUTO_CMD_INT(gPatchHalTickDelay, PatchHalTickDelay);

void patchserverdbAsyncTick(void)
{
	static U32 lastHalTick = 0;
	U32 now = timeSecondsSince2000();
	PERFINFO_AUTO_START_FUNC();
	if(!patchpruningPruneAsyncTick())
	{
		patchcompactionCompactHogsAsyncTick();
		patchcompactionCleanUpAsyncTick();
	}
	if(now >= lastHalTick + gPatchHalTickDelay)
	{
		FOR_EACH_IN_EARRAY(g_patchserver_config.serverdbs, PatchServerDb, serverdb);
		{
			// Don't tick ignored DBs
			if(g_patchserver_config.prune_config && eaFindString(&g_patchserver_config.prune_config->ignore_projects, serverdb->name) != -1)
				continue;
			patchHALTick(serverdb);
		}
		FOR_EACH_END;
		lastHalTick = now;
	}
	PERFINFO_AUTO_STOP_FUNC();
}

bool patchserverdbAsyncIsRunning(void)
{
	return patchpruningAsyncIsRunning() || patchcompactionCompactHogsAsyncIsRunning();
}

// Aborts all running async queues (prunes and compacts)
void patchserverdbAsyncAbort(void)
{
	patchpruningAsyncAbort();

	patchcompactionAsyncAbort();
}

bool patchserverdbLoad(PatchServerDb *serverdb, bool verify_hoggs, char **verify_projects, bool fix_hoggs, bool verify_hoggs_load_data,
					   bool fatalerror_on_verify_failure, bool merging)
{
	int i, j;
	char fname[MAX_PATH];
	const char *old_basedb;
	bool hierarchy_changed = false;
	PatchBranch **branches = NULL;

	if(!serverdb->name)
	{
		FatalErrorf("Database specified in server config without a name");
		return hierarchy_changed;
	}

	sprintf(fname, "./%s.patchserverdb", serverdb->name);
	if(!fileExists(fname))
	{
		printf("Warning: Database %s specified in server config, but %s does not exist! Related projects will be unavailable.\n", serverdb->name, fname);
		return hierarchy_changed;
	}

	if(!serverdb->db) // first load
	{
		loadstart_printf("Loading %s...", fname);
	}
	else
	{
		old_basedb = serverdb->basedb_name; // pooled
		StructDeInit(parse_PatchServerDb, serverdb);
		StructInitFields(parse_PatchServerDb, serverdb);
	}

	if(!ParserReadTextFile(fname, parse_PatchServerDb, serverdb, PARSER_OPTIONALFLAG))
	{
		FatalErrorf("Could not load %s", fname);
	}

	if(!serverdb->persist_prefix)
	{
		serverdb->persist_prefix = StructAllocString(""); // this could probably use some checking
	}

	// Fixup branches
	for(i = 0; i < eaSize(&serverdb->branches); i++)
	{
		PatchBranch *branch = serverdb->branches[i];
		if(!branch->branch)
			branch->branch = i;
		if(eaGet(&branches, branch->branch))
			FatalErrorf("Duplicate branch definition for branch %d", branch->branch);
		if(branch->branch >= eaSize(&branches))
			eaSetSize(&branches, branch->branch+1);
		branches[branch->branch] = branch;
	}
	for(i = serverdb->min_branch; i <= serverdb->max_branch; i++)
	{
		if(!eaGet(&branches, i))
		{
			FatalErrorf("No branch definition for branch %d (min branch %d, max branch %d)", i, serverdb->min_branch, serverdb->max_branch);
		}
	}		
	eaDestroy(&serverdb->branches);
	serverdb->branches = branches;

	if(serverdb->db) // reload
	{
		if(old_basedb != serverdb->basedb_name && stricmp(old_basedb, serverdb->basedb_name) != 0)
		{
			printf("Warning: Database hierarchy changed!! (basedb of %s changed from %s to %s) Attempting to handle this gracefully.\n",
							serverdb->name, old_basedb, serverdb->basedb_name);
			log_printf(LOG_PATCHSERVER_GENERAL, "basedb of serverdb %s changed from %s to %s, attempting fix",
							serverdb->name, old_basedb, serverdb->basedb_name);

			// I think this is safe... -GG
			hierarchy_changed = true;
			serverdb->basedb = NULL;
		}

		for(i = 0; i < eaSize(&serverdb->project_names); i++)
		{
			for(j = 1; j < eaSize(&serverdb->projects); j++)
				if(!stricmp(serverdb->project_names[i], serverdb->projects[j]->name))
					break;
			if(j > eaSize(&serverdb->projects)) // in the new config, but not loaded
				eaPush(&serverdb->projects, patchprojectLoad(serverdb->project_names[i], serverdb));
		}
		for(j = eaSize(&serverdb->projects)-1; j > 0; --j) // intentionally > (the first element is the db project)
		{
			for(i = 0; i < eaSize(&serverdb->project_names); i++)
				if(!stricmp(serverdb->project_names[i], serverdb->projects[j]->name))
					break;
			if(i > eaSize(&serverdb->project_names)) // loaded, but not in the new config
				patchprojectClear(eaRemoveFast(&serverdb->projects, j));
		}

		if(serverdb->keepdays || serverdb->keepvers)
		{
			filespecMapDestroy(serverdb->frozenmap);
			serverdb->frozenmap = patchserverdbCreateFilemapFromConfig(&serverdb->frozenmap_config);
			patchSetFrozenFiles(serverdb->db, serverdb->frozenmap);

			filespecMapDestroy(serverdb->keepdays);
			filespecMapDestroy(serverdb->keepvers);
			serverdb->keepdays = patchserverdbCreateFilemapFromConfig(&serverdb->keepdays_config);
			serverdb->keepvers = patchserverdbCreateFilemapFromConfig(&serverdb->keepvers_config);
		}

		serverdb->load_me = false;
		return hierarchy_changed;
	}

	eaPush(&serverdb->projects, patchprojectCreateDbProject(serverdb, fname));
	for(i = 0; i < eaSize(&serverdb->project_names); i++)
	{
		eaPush(&serverdb->projects, patchprojectLoad(serverdb->project_names[i], serverdb));
	}

	sprintf(fname, "./%s.manifest", serverdb->name);
	loadstart_printf("Loading %s...", fname);
	serverdb->frozenmap = patchserverdbCreateFilemapFromConfig(&serverdb->frozenmap_config);
	serverdb->db = patchLoadDb(fname, PATCHDB_POOLED_PATHS, serverdb->frozenmap);
	assert(serverdb->db);
	serverdb->db = patchLinkDb(serverdb->db, false);
	loadend_printf("");

	if(!merging)
	{
		journalRename(serverdb->name); // FIXME: this won't work if there's already a journal waiting to be merged
	}
	
	if(journalMerge(serverdb->db, serverdb->name))
	{
		serverdb->save_me = true;
	}

	patchDbRemoveBadCheckouts(serverdb->db);

	// Calculate latest revision information.
	serverdb->latest_rev = eaSize(&serverdb->db->checkins)-1;
	serverdb->latest_branch = serverdb->max_branch; // JE: Changed this so that if tip is empty, it's still the latest!

	serverdb->hogg_stash = stashTableCreateInt(64);
	if(!merging)
	{
		patchcompactionCleanUpTempHogsOnRestart(serverdb); // depends on existence of hogg_stash
	}

	// Run a verify if requested.
	if(verify_hoggs)
	{
		if (verify_projects)
		{
			EARRAY_CONST_FOREACH_BEGIN(verify_projects, k, n);
			{
				if (!stricmp_safe(verify_projects[k], serverdb->name))
					verifyHoggs(serverdb, fix_hoggs, verify_hoggs_load_data, fatalerror_on_verify_failure);
			}
			EARRAY_FOREACH_END;
		}
		else
			verifyHoggs(serverdb, fix_hoggs, verify_hoggs_load_data, fatalerror_on_verify_failure);
	}

	serverdb->load_me = false;
	loadend_printf("");
	assert(!hierarchy_changed);
	return hierarchy_changed;
}

PatchFile* patchserverdbGetFullManifestPatch(PatchServerDb *serverdb, PatchFile **patch)
{
	// TODO: this causes a significant stall
	if(!*patch)
	{
		U32 filetime = 0;
		char *estr = NULL;

		PERFINFO_AUTO_START("Creating full manifest patch", 1);

		serverdb->db->latest_rev = serverdb->latest_rev;
		if(eaSize(&serverdb->db->checkins))
			filetime = serverdb->db->checkins[eaSize(&serverdb->db->checkins)-1]->time;
		patchDbWrite(NULL, &estr, serverdb->db);
		*patch = patchfileFromEString(&estr, filetime);

		PERFINFO_AUTO_STOP();
	}
	return *patch;
}

static void patchserverdbGetIncrementalManifestCopyDirEntries(PatchServerDb *serverdb, DirEntry *new_de, DirEntry *old_de, int from_rev)
{
	// Copy over the name
	if(old_de->name)
		new_de->name = strdup(old_de->name);

	// Copy over the file versions
	FOR_EACH_IN_EARRAY_FORWARDS(old_de->versions, FileVersion, old_ver)
		if(old_ver->rev >= from_rev && old_ver->rev <= serverdb->latest_rev)
		{
			FileVersion *new_ver = StructClone(parse_FileVersion, old_ver);
			eaPush(&new_de->versions, new_ver);
		}
	FOR_EACH_END

	// Copy over checkouts
	FOR_EACH_IN_EARRAY_FORWARDS(old_de->checkouts, Checkout, old_checkout)
		Checkout *new_checkout = StructClone(parse_Checkout, old_checkout);
		eaPush(&new_de->checkouts, new_checkout);
	FOR_EACH_END

	// Copy over the children
	FOR_EACH_IN_EARRAY_FORWARDS(old_de->children, DirEntry, old_child)
		DirEntry *new_child = StructCreate(parse_DirEntry);
		patchserverdbGetIncrementalManifestCopyDirEntries(serverdb, new_child, old_child, from_rev);
		if(eaSize(&new_child->versions) || eaSize(&new_child->children) || eaSize(&new_child->checkouts))
			eaPush(&new_de->children, new_child);
		else
			StructDestroy(parse_DirEntry, new_child);
	FOR_EACH_END
}

PatchFile* patchserverdbGetIncrementalManifestPatch(PatchServerDb *serverdb, int from_rev, PatchFile **patch)
{
	PatchDB *db;
	U32 filetime = 0;
	char *estr = NULL;
	int i;
	PatchFile *file;

	PERFINFO_AUTO_START_FUNC();

	// If there's a manifest for this link, use that.
	if (*patch)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return *patch;
	}

	// Otherwise, look for a cached one.
	for(i=0; i<eaSize(&serverdb->incremental_manifest_patch); i++)
	{
		if(serverdb->incremental_manifest_revs[i] == from_rev)
		{
			*patch = patchfileDup(serverdb->incremental_manifest_patch[i]);
			return *patch;
		}
	}

	db = StructCreate(parse_PatchDB);

	// Copy over the version
	db->version = serverdb->db->version;

	// Copy over needed checkins
	FOR_EACH_IN_EARRAY_FORWARDS(serverdb->db->checkins, Checkin, checkin)
		if(checkin->rev >= from_rev && checkin->rev <= serverdb->latest_rev)
			eaPush(&db->checkins, checkin);
	FOR_EACH_END

	// Copy over needed views
	FOR_EACH_IN_EARRAY_FORWARDS(serverdb->db->namedviews, NamedView, view)
		if(view->rev >= from_rev && view->rev <= serverdb->latest_rev)
			eaPush(&db->namedviews, view);
	FOR_EACH_END

	// Create the restricted file tree
	patchserverdbGetIncrementalManifestCopyDirEntries(serverdb, &db->root, &serverdb->db->root, from_rev);

	// Copy the branch validity
	db->branch_valid_since = serverdb->db->branch_valid_since;

	// Copy the latest rev
	db->latest_rev = serverdb->latest_rev;

	// Write to a string
	patchDbWrite(NULL, &estr, db);
	devassert(estrLength(&estr));

	// Deallocate the new DB
	db->branch_valid_since = NULL;
	eaDestroy(&db->checkins);
	eaDestroy(&db->namedviews);
	StructDestroy(parse_PatchDB, db);

	// Create and return a fake patchfile
	if(eaSize(&serverdb->db->checkins))
		filetime = serverdb->db->checkins[eaSize(&serverdb->db->checkins)-1]->time;
	file = patchfileFromEString(&estr, filetime);
	eaiPush(&serverdb->incremental_manifest_revs, from_rev);
	eaPush(&serverdb->incremental_manifest_patch, file);

	*patch = patchfileDup(file);

	PERFINFO_AUTO_STOP_FUNC();

	return *patch;
}

FileVersion* patchserverdbAddFile(	PatchServerDb *serverdb,
									const char *dir_name,
									void *data,
									int size_uncompressed,
									int size_compressed,
									U32 checksum,
									U32 timeModified,
									U32 header_size,
									U32 header_checksum,
									U8 *header_data,
									Checkin *checkin)
{
	FileVersion*		ver;
	bool				deleted = false;
	HALHogFile*			halhog;
	FileNameAndOldName	pathInHogg;

	if(size_uncompressed < 0)
	{
		deleted = true;
		size_uncompressed = 0;
	}
	ver = patchAddVersion(	serverdb->db,
							checkin,
							0,
							deleted,
							dir_name,
							timeModified,
							size_uncompressed,
							header_size,
							header_checksum,
							header_data,
							0);

	halhog = patchHALGetWriteHogHandle(serverdb, checkin->time, true);
	assertmsgf(halhog, "Unable to get hogg handle for checking to %s", serverdb->name);
	patchserverdbNameInHogg(serverdb, ver, &pathInHogg);
	patchserverdbWriteFile(	serverdb->name,
							halhog->hog,
							pathInHogg.name,
							data,
							size_uncompressed,
							size_compressed,
							checksum,
							timeModified,
							checkin,
							serverdb->mirrorHoggsLocally);

	if(checksum)
	{
		ver->checksum = checksum;
	}
	else
	{
		HogFileIndex hfi = hogFileFind(halhog->hog, pathInHogg.name);
		ver->checksum = hogFileGetFileChecksum(halhog->hog, hfi);
	}
	
	patchHALHogFileDestroy(halhog, false);

	return ver;
}

typedef struct WriteToDiskFile {
	char*			dbName;
	char*			pathInHogg;
	char*			data;
	U32				sizeCompressed;
	U32				sizeUncompressed;
	U32				timeCheckin;
	U32				timeModified;
	U32				refCount;
	S32				doPrint;
} WriteToDiskFile;

static struct {
	CRITICAL_SECTION		cs;
	U64						bytesToWrite;
	WriteToDiskFile**		fileQueue;
	StashTable				stDataToFile;
} writesToDisk;

static void writeToDiskFileDecRefCount(WriteToDiskFile* f){
	assert(f->refCount);
	
	if(!--f->refCount){
		LeaveCriticalSection(&writesToDisk.cs);
		{
			SAFE_FREE(f->dbName);
			SAFE_FREE(f->data);
			SAFE_FREE(f->pathInHogg);
			SAFE_FREE(f);
		}
		EnterCriticalSection(&writesToDisk.cs);
	}
}

static void writeToDiskFileHoggFreeCallback(char* data){
	EnterCriticalSection(&writesToDisk.cs);
	{
		WriteToDiskFile* f;

		//printf("removing from stash table: %p:%p\n", writesToDisk.stDataToFile, data);

		if(!stashRemovePointer(writesToDisk.stDataToFile, data, &f)){
			assert(0);
		}
		
		assert(f && f->data == data);
		
		writeToDiskFileDecRefCount(f);
	}
	LeaveCriticalSection(&writesToDisk.cs);
}

static void* writeToDiskZAlloc(void* opaque, U32 items, U32 size)
{
	return malloc(items * size);
}

static void writeToDiskZFree(void* opaque, void* address)
{
	SAFE_FREE(address);
}

void patchserverdbGetHoggMirrorFilePath(char* filePathOut,
									size_t filePathOutSize,
									const char* dbName,
									const char* pathInHogg,
									U32 timeCheckin)
{
	size_t len;
	
	strcpy_s(	filePathOut,
				filePathOutSize,
				"./NoBackup/HoggMirror/");
				
	len = strlen(filePathOut);
			
	patchHALGetHogFileNameFromTimeStamp(	filePathOut + len,
									filePathOutSize - len,
									dbName,
									timeCheckin);
									
	strcatf_s(	filePathOut,
				filePathOutSize,
				"/%s",
				pathInHogg);
}									

static void writeToDiskFileWrite(const WriteToDiskFile* f){
	char writeFileName[MAX_PATH * 2];
	
	patchserverdbGetHoggMirrorFilePath(	SAFESTR(writeFileName),
							f->dbName,
							f->pathInHogg,
							f->timeCheckin);
	
	if(strlen(writeFileName) >= MAX_PATH-20)
	{
		// Use MAX_PATH-20 so there is room for the absolute path at the start, since fwStat will add that.
		// Filename too long, ignore it.
		if(f->doPrint)
		{
			printfColor(COLOR_BRIGHT|COLOR_RED, "Not writing hogg mirror %s: path too long", writeFileName);
		}
		log_printf(LOG_PATCHSERVER_MIRRORING, "Not writing hogg mirror %s: path too long", writeFileName);
		return;
	}

	if(!f->data){
		// Delete the file.
		
		if(fileExists(writeFileName)){
			if(f->doPrint){
				printfColor(COLOR_BRIGHT|COLOR_GREEN, "Deleting hogg mirror: %s\n", writeFileName);
			}
			
			if(unlink(writeFileName)){
				printfColor(COLOR_BRIGHT|COLOR_RED,
							"ERROR: Failed to delete file: %s\n",
							writeFileName);
				devassert(0);
			}
		}
	}else{
		// Write the file.
		FILE* fh;

		if(f->doPrint){
			printfColor(COLOR_BRIGHT|COLOR_GREEN,
						"Writing hogg mirror (%s bytes): %s...",
						getCommaSeparatedInt(f->sizeUncompressed),
						writeFileName);
		}
		
		makeDirectoriesForFile(writeFileName);
		
		fh = fopen(writeFileName, "wb");
		
		if(!fh){
			printfColor(COLOR_BRIGHT|COLOR_RED,
						"ERROR: Can't open file for writing\n");
		}
		else if(f->sizeCompressed){
			// File is compressed, so stream-decompress it into an on-disk file.
			
			static z_stream*	z;
			static const U32	inflateBufferSize = 64 * 1024;
			static char*		inflateBuffer;
			
			U32 sizeRemaining = f->sizeUncompressed;
			S32 wroteWholeFile = 1;
			
			if(!z){
				z = callocStruct(z_stream);
				z->zalloc = writeToDiskZAlloc;
				z->zfree = writeToDiskZFree;
				inflateInit(z);
				
				inflateBuffer = malloc(inflateBufferSize);
			}
			
			inflateReset(z);
			
			z->next_in = f->data;
			z->avail_in = f->sizeCompressed;
			
			while(sizeRemaining){
				const U32	curSizeToWrite = min(sizeRemaining, inflateBufferSize);
				S32			ret;
				
				z->avail_out = curSizeToWrite;
				z->next_out = inflateBuffer;
				
				ret = inflate(z, Z_NO_FLUSH);

				if(	ret != Z_OK &&
					ret != Z_STREAM_END)
				{
					wroteWholeFile = 0;
					log_printf(LOG_PATCHSERVER_ERRORS,
									"Failed to decompress file when writing to disk: %s\n",
									writeFileName);
					break;
				}
				
				if(!fwrite(inflateBuffer, curSizeToWrite, 1, fh)){
					log_printf(LOG_PATCHSERVER_ERRORS,
									"Failed to write decompressed chunk to disk: %s\n",
									writeFileName);
					break;
				}
				
				sizeRemaining -= curSizeToWrite;
			}
			
			if(!wroteWholeFile){
				log_printf(LOG_PATCHSERVER_ERRORS,
								"Failed to mirror file to disk: %s\n",
								writeFileName);
			}
		}else{
			// Already decompressed, so just dump it to disk.
			
			if(f->sizeUncompressed){
				const U8*	curData = f->data;
				U32			bytesRemaining = f->sizeUncompressed;
				
				while(bytesRemaining){
					U32 curBytes = min(bytesRemaining, 64 * 1024);
					
					if(!fwrite(curData, curBytes, 1, fh)){
						if(f->doPrint){
							printfColor(COLOR_BRIGHT|COLOR_RED, "ERROR: Failed to write data.\n");
						}
						
						log_printf(LOG_PATCHSERVER_ERRORS,
										"Failed to write entire file to disk: %s\n",
										writeFileName);
									
						break;
					}
					
					bytesRemaining -= curBytes;
					curData += curBytes;
				}
			}
		}
		
		if(f->doPrint){
			printfColor(COLOR_BRIGHT|COLOR_GREEN, "done.\n");
		}

		if(fh){
			fclose(fh);
			fh = NULL;

			fileSetTimestamp(writeFileName, f->timeModified);
		}
	}
}

static U32 __stdcall writeToDiskThread(void* unused){
	EXCEPTION_HANDLER_BEGIN

	WriteToDiskFile* f = NULL;
	
	while(1){
		autoTimerThreadFrameBegin("DbWriteToDisk");
		Sleep(1);
		if(	f ||
			writesToDisk.fileQueue)
		{
			EnterCriticalSection(&writesToDisk.cs);
			{
				if(f){
					assert(writesToDisk.bytesToWrite >= f->sizeUncompressed);
					writesToDisk.bytesToWrite -= f->sizeUncompressed;
					
					writeToDiskFileDecRefCount(f);
					
					f = NULL;
				}
				
				if(eaSize(&writesToDisk.fileQueue)){
					f = writesToDisk.fileQueue[0];
					
					assert(f);
					
					if(eaSize(&writesToDisk.fileQueue) == 1){
						eaDestroy(&writesToDisk.fileQueue);
					}else{
						eaRemove(&writesToDisk.fileQueue, 0);
					}
				}
			}
			LeaveCriticalSection(&writesToDisk.cs);
			
			if(f){
				writeToDiskFileWrite(f);
			}
		}
		autoTimerThreadFrameEnd();
	}
	
	EXCEPTION_HANDLER_END
	
	return 0;
}

S32 patchserverdbGetHoggMirrorQueueSize(U64* bytesToWriteOut){
	S32 size = 0;
	
	if(writesToDisk.fileQueue){
		EnterCriticalSection(&writesToDisk.cs);
		{
			size = eaSize(&writesToDisk.fileQueue);
			
			if(bytesToWriteOut){
				*bytesToWriteOut = writesToDisk.bytesToWrite;
			}
		}
		LeaveCriticalSection(&writesToDisk.cs);
	}else{
		if(bytesToWriteOut){
			*bytesToWriteOut = 0;
		}
	}
	
	return size;
}

void patchserverdbQueueWriteToDisk(	const char* dbName,
											const char* pathInHogg,
											char* data,
											U32 timeCheckin,
											U32 timeModified,
											U32 sizeCompressed,
											U32 sizeUncompressed,
											S32 doWaitForHoggToFinish)
{

	WriteToDiskFile* f;

	f = callocStruct(WriteToDiskFile);
	f->data = data;
	f->pathInHogg = strdup(pathInHogg);
	f->dbName = strdup(dbName);
	f->timeCheckin = timeCheckin;
	f->timeModified = timeModified;
	f->sizeCompressed = sizeCompressed;
	f->sizeUncompressed = sizeUncompressed;
	f->refCount = 1 + (data && doWaitForHoggToFinish);
	f->doPrint = !doWaitForHoggToFinish;
	
	ATOMIC_INIT_BEGIN;
	{
		InitializeCriticalSection(&writesToDisk.cs);
		writesToDisk.stDataToFile = stashTableCreateAddress(100);
		_beginthreadex(NULL, 0, writeToDiskThread, NULL, 0, NULL);
	}
	ATOMIC_INIT_END;
	
	EnterCriticalSection(&writesToDisk.cs);
	{
		//printf(	"adding to stash table: %p:%p (size %d/%d)\n",
		//		writesToDisk.stDataToFile,
		//		f->data,
		//		f->sizeCompressed,
		//		f->sizeUncompressed);
		
		if(	data &&
			doWaitForHoggToFinish)
		{
			if(!stashAddPointer(writesToDisk.stDataToFile,
								data,
								f,
								0))
			{
				assertmsg(0, "Duplicate data address writing file to disk.");
			}
		}
		
		//printf("added  to stash table: %p:%p\n", writesToDisk.stDataToFile, f->data);

		eaPush(&writesToDisk.fileQueue, f);
		
		writesToDisk.bytesToWrite += f->sizeUncompressed;
	}
	LeaveCriticalSection(&writesToDisk.cs);
}

void patchserverdbQueueDeleteToDisk(	const char* dbName,
											const char* filePath,
											U32 timeCheckin)
{
	patchserverdbQueueWriteToDisk(	dbName,
									filePath,
									NULL,
									timeCheckin,
									0,
									0,
									0,
									0);
}

void patchserverdbWriteFile(const char* dbName,
							HogFile* hogg,
							const char* pathInHogg,
							void* data,
							int size_uncompressed,
							int size_compressed,
							U32 checksum,
							U32 timeModified,
							const Checkin* checkin,
							S32 mirrorHoggsLocally)
{
	NewPigEntry			entry = {0};
	
	entry.data = data ? data : malloc(0);
	entry.fname = pathInHogg;
	entry.size = size_uncompressed;
	entry.pack_size = entry.size ? max(0, size_compressed) : 0;
	entry.timestamp = timeModified;
	entry.checksum[0] = entry.pack_size ? checksum : 0;
	entry.must_pack = !!entry.pack_size; 

	if(mirrorHoggsLocally){
		entry.free_callback = writeToDiskFileHoggFreeCallback;
		
		patchserverdbQueueWriteToDisk(	dbName,
										pathInHogg,
										entry.data,
										checkin->time,
										timeModified,
										entry.pack_size,
										entry.size,
										1);
	}

	SERVLOG_PAIRS(LOG_PATCHSERVER_INFO, "WriteFile",
		("hogfile", "%s", hogFileGetArchiveFileName(hogg))
		("file", "%s", entry.fname)
		("size", "%d", entry.size)
		("pack_size", "%lu", entry.pack_size)
		("modified", "%lu", entry.timestamp));
	hogFileModifyUpdateNamed2(hogg, &entry);
}

int patchserverdbAddCheckouts(	PatchServerDb *serverdb,
								DirEntry **dirs,
								const char *author,
								int branch,
								const char *sandbox,
								char* errMsg,
								S32 errMsg_size)
{
	int i;
	U32 now = getCurrentFileTime();
	PatchJournal *journal;
	JournalCheckout *jcheckout;
	char authorTrimmed[MAX_PATH];
	
	strcpy(authorTrimmed, author);
	removeLeadingAndFollowingSpaces(authorTrimmed);
	author = authorTrimmed;
	
	if(!author[0]){
		printfColor(COLOR_BRIGHT|COLOR_RED,
					"Trying to add checkout with blank author:\n");
					
		for(i = 0; i < eaSize(&dirs); i++)
		{
			printfColor(COLOR_BRIGHT|COLOR_RED,
						"  %s\n",
						dirs[i]->path);
		}
		
		if(errMsg){
			sprintf_s(	SAFESTR2(errMsg),
						"The author is blank");
		}
		
		return 0;
	}
	
	journal = journalCreate(eaSize(&serverdb->db->checkins) - 1);
	jcheckout = journalAddCheckout(journal, author, sandbox, branch, now);
	for(i = 0; i < eaSize(&dirs); i++)
	{
		patchAddCheckout(serverdb->db, dirs[i], author, branch, sandbox, now);
		journalAddCheckoutFile(jcheckout, dirs[i]->path);
	}
	journalFlushAndDestroy(&journal, serverdb->name);
	
	return 1;
}

void patchserverdbRemoveCheckouts(PatchServerDb *serverdb, DirEntry **dirs, int branch, const char *sandbox)
{
	int i;
	PatchJournal *journal = journalCreate(eaSize(&serverdb->db->checkins) - 1);
	JournalRemoveCheckout *uncheckout = journalRemoveCheckout(journal, branch, sandbox);
	for(i = 0; i < eaSize(&dirs); i++)
	{
		patchRemoveCheckout(serverdb->db, dirs[i], branch, sandbox);
		journalRemoveCheckoutFile(uncheckout, dirs[i]->path);
	}
	journalFlushAndDestroy(&journal, serverdb->name);
}

// Log that a file was deleted from a hog.
void patchserverdbHogDelete(PatchServerDb *serverdb, HogFile *hogfile, const char *filename, const char *reason)
{
	SERVLOG_PAIRS(LOG_PATCHSERVER_INFO, "DeletedFromHog",
		("hogfile", "%s", hogFileGetArchiveFileName(hogfile))
		("filename", "%s", filename)
		("reason", "%s", reason)
	);
	patchHALHogDelete(serverdb, hogfile, filename);
}

bool patchserverdbRequestAsyncLoadFromHogg(PatchFile *patch, PatchServerDb *serverdb, U32 checkin_time, const char *filename, U32 priority, bool uncompressed,
										FileLoaderHoggFunc background, FileLoaderHoggFunc foreground)
{
	HALHogFile* halhog = patchHALGetReadHogHandleFromFilename(serverdb, checkin_time, filename);
	
	if (!halhog)
	{
		if(patch->halhog)
		{
			devassertmsgf(0, "Failed to open hog handle for (%s, %u, %s), but already have one open for (%s, %u, %s).", serverdb->name, checkin_time, filename, patch->serverdb->name, patch->checkin_time, patch->fileName.name);
			patchHALHogFileDestroy(patch->halhog, true);
			patch->halhog = NULL;
		}
		return false;
	}

	if(patch->halhog && patch->halhog->hog != halhog->hog)
	{
		patchHALHogFileDestroy(patch->halhog, true);
		patch->halhog = NULL;
	}

	if(!patch->halhog)
	{
		patch->halhog = patchHALHogFileAddRef(halhog);
	}

	fileLoaderRequestAsyncLoadFromHogg( halhog->hog,
										filename,
										priority,
										uncompressed,
										background,
										foreground,
										patch);
	
	patchHALHogFileDestroy(halhog, false);
	return true;
}

void patchserverdbSetVersionSize(PatchServerDb *serverdb, FileVersion *v)
{
	FileNameAndOldName	pathInHogg;
	HALHogFile* halhog;
	
	patchserverdbNameInHogg(serverdb, v, &pathInHogg);
	halhog = patchHALGetReadHogHandle(serverdb, v->checkin->time, &pathInHogg);
	
	if(halhog)
	{
		HogFileIndex		hfi;
		
		hfi = hogFileFind(halhog->hog, pathInHogg.name);
		
		if(hfi == HOG_INVALID_INDEX)
		{
			hfi = hogFileFind(halhog->hog, pathInHogg.oldName);
		}
		
		if(hfi != HOG_INVALID_INDEX)
		{
			U32 bytesCompressed;
			U32 bytesUncompressed;
			
			v->foundInHogg = 1;

			hogFileGetSizes(halhog->hog, hfi, &bytesUncompressed, &bytesCompressed);
			
			v->sizeInHogg = FIRST_IF_SET(	bytesCompressed,
											bytesUncompressed);
		}
		patchHALHogFileDestroy(halhog, false);
	}
}

bool patchserverdbGetDataForVersion(PatchServerDb *serverdb, FileVersion *version, FileNameAndOldName *name, char **data, U32 *len, U32 *len_compressed)
{
	HogFileIndex hfi;
	U32 tmp;
	HALHogFile *halhog;
	patchserverdbNameInHogg(serverdb, version, name);
	halhog = patchHALGetReadHogHandle(serverdb, version->checkin->time, name);
	assertmsgf(halhog, "Can't find hogg to load undo data: %s %u", serverdb->name, version->checkin->time);
	hfi = hogFileFind(halhog->hog, name->name);
	if(hfi == HOG_INVALID_INDEX)
		hfi = hogFileFind(halhog->hog, name->oldName);
	if(hfi == HOG_INVALID_INDEX)
	{
		hogFileDestroy(halhog->hog, false);
		return false;
	}
	hogFileGetSizes(halhog->hog, hfi, len, len_compressed);
	if(len_compressed)
		*data = hogFileExtractCompressed(halhog->hog, hfi, &tmp);
	else
		*data = hogFileExtract(halhog->hog, hfi, &tmp, NULL);

	patchHALHogFileDestroy(halhog, false);

	return true;
}

void patchserverdbRemoveVersion(PatchServerDb *serverdb, const Checkin *checkin, FileVersion *ver)
{
	HALHogFile *halhog;

	PERFINFO_AUTO_START_FUNC();

	// Delete the version file data from hoggs.
	// We are turning off the alert because there is a chance the file might not exist, and this is an expected possibility.
	// This is a problem introduced by full sync.
	// Must go through tempHogg if it exists.
	PERFINFO_AUTO_START("DeleteFromHoggs", 1);
	halhog = patchHALGetWriteHogHandleNoAlert(serverdb, checkin->time, false);
	if(halhog)
	{
		FileNameAndOldName removeFile;
		patchserverdbNameInHogg(serverdb, ver, &removeFile);
		patchserverdbHogDelete(serverdb, halhog->hog, removeFile.name, "UnusedVersionFile");
		patchserverdbHogDelete(serverdb, halhog->hog, removeFile.oldName, "UnusedVersionFileOld");
		patchHALHogFileDestroy(halhog, false);
	}
	PERFINFO_AUTO_STOP_CHECKED("DeleteFromHoggs");

	// Delete the patch file.
	patchfileDestroy(&ver->patch);

	// Remove it from the PatchDB.
	fileVersionRemoveAndDestroy(serverdb->db, ver);

	PERFINFO_AUTO_STOP_FUNC();
}

#define ECHO_log_printfS 1

#ifdef ECHO_log_printfS
#define ERROR_PRINTF(format, ...) {log_printf(LOG_PATCHSERVER_ERRORS, format, __VA_ARGS__); printf(format, __VA_ARGS__);}
#define INFO_PRINTF(format, ...) {log_printf(LOG_PATCHSERVER_INFO, format, __VA_ARGS__); printf(format, __VA_ARGS__);}
#define CONNECTION_PRINTF(format, ...) log_printf(LOG_PATCHSERVER_CONNECTIONS, format, __VA_ARGS__)
#else
#define ERROR_PRINTF(...) log_printf(LOG_PATCHSERVER_ERRORS, __VA_ARGS__)
#define INFO_PRINTF(...) log_printf(LOG_PATCHSERVER_INFO, __VA_ARGS__)
#define CONNECTION_PRINTF(...) log_printf(LOG_PATCHSERVER_CONNECTIONS, __VA_ARGS__)
#endif

void patchserverdbLoadHeader(FileVersion *ver, PatchServerDb *serverdb)
{
	HALHogFile *halhog;
	HogFileIndex hfi;
	FileNameAndOldName	pathInHogg;

	PERFINFO_AUTO_START_FUNC();

	patchserverdbNameInHogg(serverdb, ver, &pathInHogg);
	halhog = patchHALGetReadHogHandle(serverdb, ver->checkin->time, &pathInHogg);
	if(halhog)
	{
		char*				usedPathInHogg;
		
		usedPathInHogg = pathInHogg.name;
		hfi = hogFileFind(halhog->hog, usedPathInHogg);
		
		if(hfi == HOG_INVALID_INDEX)
		{
			usedPathInHogg = pathInHogg.oldName;
			hfi = hogFileFind(halhog->hog, usedPathInHogg);
		}
		
		if(hfi != HOG_INVALID_INDEX)
		{
			NewPigEntry pig_entry;
			U8*			header_data;
			U32			header_size;
			char*		found;

			// Chop off the in-hogg filename (r#####_t#####_b####, etc).

			found = strrchr(usedPathInHogg, '/');
			if(found)
				found[0] = '\0';

			// FIXME: This should use hogFileGetHeaderData()
			ZeroStruct(&pig_entry);
			pig_entry.data = hogFileExtract(halhog->hog, hfi, &pig_entry.size, NULL);
			pig_entry.fname = usedPathInHogg;

			header_data = pigGetHeaderData(&pig_entry, &header_size);
			if(header_data)
			{
				U32 new_checksum;
				ver->header_data = calloc( 1, (header_size + HEADER_BLOCK_SIZE - 1) & ~(HEADER_BLOCK_SIZE - 1) );
				memcpy(ver->header_data, header_data, header_size);
				ver->header_size = header_size;
				new_checksum = patchChecksum(ver->header_data, ver->header_size);
				if(ver->header_checksum != new_checksum)
				{
					ERROR_PRINTF("Warning: header_checksum changed for %s\n", usedPathInHogg);
				}
				ver->header_checksum = new_checksum;
			}

			SAFE_FREE(pig_entry.data);
		}
		patchHALHogFileDestroy(halhog, false);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

void patchserverdbRemoveNewVersion(PatchServerDb *serverdb, FileVersion *new_version)
{
	FileNameAndOldName	pathInHogg;
	HALHogFile*			halhog = patchHALGetWriteHogHandle(	serverdb,
														new_version->checkin->time, true);

	if(halhog)
	{
		patchserverdbNameInHogg(serverdb, new_version, &pathInHogg);
		patchserverdbHogDelete(serverdb, halhog->hog, pathInHogg.name, "CheckinFailed");
		patchHALHogFileDestroy(halhog, false);
	}
}


typedef struct ManifestStatsFile {
	const char*		dbName;
	const char*		path;
	U32				fileCount;
	U32				deletedCount;
	U64				byteCount;
} ManifestStatsFile;

typedef struct ManifestStats {
	ManifestStatsFile**		allFiles;
} ManifestStats;

static void accumulateManifestStats(ManifestStats* ms,
									const PatchServerDb* psdb,
									const DirEntry* de)
{
	if(de->path){
		ManifestStatsFile* msf;
		
		msf = callocStruct(ManifestStatsFile);
		msf->dbName = psdb->name;
		msf->path = de->path;
		
		eaPush(&ms->allFiles, msf);
		
		EARRAY_CONST_FOREACH_BEGIN(de->versions, i, isize);
			FileVersion* v = de->versions[i];
			
			if(v->flags & FILEVERSION_DELETED){
				msf->deletedCount++;
			}else{
				msf->fileCount++;
				msf->byteCount += v->size;
			}
		EARRAY_FOREACH_END;
	}
		
	EARRAY_CONST_FOREACH_BEGIN(de->children, i, isize);
		accumulateManifestStats(ms, psdb, de->children[i]);
	EARRAY_FOREACH_END;
}

static S32 compareManifestStatsFileSize(const ManifestStatsFile** msf1,
										const ManifestStatsFile** msf2)
{
	if(msf1[0]->byteCount < msf2[0]->byteCount){
		return -1;
	}
	else if(msf1[0]->byteCount > msf2[0]->byteCount){
		return 1;
	}
	else{
		return 0;
	}
}

AUTO_COMMAND ACMD_NAME(dumpManifestStats);
void cmdDumpManifestStats(const char* filename)
{
	ManifestStats	ms = {0};
	FILE*			f;

	if(fileExists(filename)){
		printf(__FUNCTION__": File already exists: %s\n", filename);
		return;
	}
	
	f = fopen(filename, "wt");

	if(!f){
		printf(__FUNCTION__": Can't open file: %s\n", filename);
		return;
	}
	
	loadstart_printf("Writing manifest stats to: %s...", filename);

	EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.serverdbs, i, isize);
		const PatchServerDb* psdb = g_patchserver_config.serverdbs[i];
		
		accumulateManifestStats(&ms, psdb, &psdb->db->root);
	EARRAY_FOREACH_END;
	
	eaQSortG(ms.allFiles, compareManifestStatsFileSize);
	
	EARRAY_CONST_FOREACH_BEGIN(ms.allFiles, i, isize);
		ManifestStatsFile* msf = ms.allFiles[i];
		
		fprintf(f,
				"%s\t%s\t%d\t%"FORM_LL"d\t%d\n",
				msf->dbName,
				msf->path,
				msf->fileCount,
				msf->byteCount,
				msf->deletedCount);
	EARRAY_FOREACH_END;
	
	fclose(f);
	
	eaDestroyEx(&ms.allFiles, NULL);
	
	loadend_printf("done.");
}

#include "patchserverdb_h_ast.c"
