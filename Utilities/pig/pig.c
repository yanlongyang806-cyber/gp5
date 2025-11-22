/***************************************************************************



***************************************************************************/
#include "FolderCache.h"
#include "wininclude.h"
#include "piglib.h"
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "file.h"
#include "utils.h"
#include "genericlist.h"
#include "gimmeDLLWrapper.h"
#include "sysutil.h"
#include "EString.h"
#include "timing.h"
#include <direct.h>
#include "SharedMemory.h"
#include "crypt.h"
#include "zlib.h"
#include "winfiletime.h"
#include "hoglib.h"
#include "hogutil.h"
#include "MemoryMonitor.h"
#include "StashTable.h"
#include "patchtest.h"
#include "heavyTest.h"
#include "mathutil.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "strings_opt.h"
#include "StringCache.h"
#include "threadedFileCopy.h"
#include "textparser.h"
#include "ListView.h"
#include "resource.h"
#include "winutil.h"
#include "utilitiesLib.h"
#include "endian.h"
#include "logging.h"
#include "FilespecMap.h"
#include "UnitSpec.h"
#include "fileWatch.h"
#include "MemAlloc.h"
#include "MemoryMonitor.h"
#include "ThreadManager.h"
#include "timing_profiler_interface.h"
#include "qsortG.h"
#ifndef _XBOX
#include <CommCtrl.h>
#endif
#include <winbase.h>
#include "UTF8.h"

#include "AutoGen/pig_c_ast.h"

// #define loadstart_printf if (verbose) loadstart_printf
// #define loadend_printf if (verbose) loadend_printf


static int pause=0;
extern void *extractFromFS(const char *name, U32 *count);  // in piglib.c

static char base_folder[MAX_PATH];
static int verbose=0;
static int debug_verbose=0;
static int overwrite=0;
static char **filespecs=NULL;
static NewPigEntry *pig_entries;
static int pig_entry_count,pig_entry_max;
static int base_folder_set=0;
static bool ignore_underscores=true;
static bool preserve_pathname=false;
static FILE *g_fileListDump;
static char *g_fileListSourceName;
static char *g_mirrorFileSpecName;
static SimpleFileSpec *g_mirrorFileSpec;
static bool fix_on_first_verify;
static bool shutdown_remote_server;
static bool ignore_timestamps_on_diff;
static HogDiffFlags hog_diff_flags;
static bool repair_on_first_open;
static int default_journal_size=0; // Use Hogg default
static U64 sync_max_size=2*1024*1024*1024LL;
static HogFileCreateFlags global_flags = HOG_DEFAULT;
static HogDefragFlags defrag_flags = HogDefrag_Default;
static bool invert_filespec;
static bool dont_pack;

enum {
	PIG_CREATE=1,
	PIG_LIST,
	PIG_EXTRACT,
	PIG_GENLIST,
	PIG_UPDATE,
	PIG_WLIST,
	PIG_VERIFY,
	PIG_SYNC,
	PIG_ADD,
	PIG_DELETE,
	PIG_DEFRAG,
	PIG_DIFF,
	PIG_LOCK,
	PIG_MASSDELETE,
};

static void pak() {
	extern void hogThreadingStopTiming(void);
	hogThreadingStopTiming();
#ifndef _XBOX
	if (pause) {
		int c;
		printf("Press any key to continue...\n");
		c = _getch();
	} else
#endif
	{
		printf("\n");
	}
}

static int strEndsWith(const char *ref, const char *ending) {
	return stricmp(ending, ref+strlen(ref)-strlen(ending))==0;
}

static void checksum(U8 *data,int count,U32 *checksum)
{
	cryptMD5Update(data,count);
	cryptMD5Final(checksum);
}

static bool eq(const char c0, const char c1) {
	return c0==c1 || (c0=='/' && c1=='\\') || (c0=='\\' && c1=='/') || toupper((unsigned char)c0)==toupper((unsigned char)c1);
}

static int checkFileSpec(const char *filename) { // returns 1 if it passes
	int i;
	char temp[MAX_PATH];
	char *c;
	int ret = 0;
	
	strcpy(temp, filename);
	for (c=temp; *c; c++ ) *c = toupper(*c);

	if (eaSize(&filespecs)==0)
		return 1;
	for (i=0; i<eaSize(&filespecs); i++) {
		if (matchExact(filespecs[i], temp)) {
			ret=1;
		}
		if (filespecs[i][0]=='!' && matchExact(filespecs[i]+1, temp)) {
			ret=0;
		}
	}
	if (invert_filespec)
		return !ret;
	return ret;
}

FileScanAction pigInputProcessor(char* dir, struct _finddata32_t* data, void *pUserData) {
	char filename[MAX_PATH];
	char *fn;
	const char *ext;
	static int count=0;

	if (data->name[0]=='_' && ignore_underscores) {
		return FSA_NO_EXPLORE_DIRECTORY;
	}
	if (data->attrib & _A_SUBDIR) {
		return FSA_EXPLORE_DIRECTORY;
	}
	ext = strrchr(data->name, '.');
	if (ext) {
		if (stricmp(ext, ".bak")==0 || stricmp(ext, ".pigg")==0)
		{
			return FSA_EXPLORE_DIRECTORY;
		}
	}
	STR_COMBINE_SSS(filename, dir, "/", data->name);
	fn = filename + strlen(base_folder);
	if (*fn=='/') fn++;

	count++;
	if ((count%500)==0)
		printf(".");
	// check against filespec
	if (!checkFileSpec(fn)) {
		return FSA_EXPLORE_DIRECTORY;
	}
	forwardSlashes(filename);
	fixDoubleSlashes(filename);
	assert(strnicmp(filename, base_folder, strlen(base_folder))==0);

	if (g_fileListDump) {
		int slen = (int)strlen(fn);
		U32 timestamp = data->time_write;
		fwrite(&slen, sizeof(slen), 1, g_fileListDump);
		fwrite(fn, 1, slen, g_fileListDump);
		fwrite(&timestamp, sizeof(timestamp), 1, g_fileListDump);
		fwrite(&data->size, sizeof(data->size), 1, g_fileListDump);
	} else {
		NewPigEntry *entry;
		entry = dynArrayAdd(pig_entries,sizeof(pig_entries[0]),pig_entry_count,pig_entry_max,1);
		entry->fname = strdup(fn);
		entry->timestamp = data->time_write;
		entry->data = NULL;
		entry->size = data->size;
		entry->dont_pack = dont_pack;
	}

	return FSA_EXPLORE_DIRECTORY;
}

static int addPig(const char *filename, const char *out)
{
	int ret=0;
	HogFile *hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|HOG_MUST_BE_WRITABLE|global_flags);

	if (hog_file )
	{
		U32 len;
		void *data = fileAlloc(filename, &len);
		U32 timestamp = fileLastChanged(filename);
		if (!data) {
			printf("Error opening %s\n", filename);
			ret = -1;
		} else {
			hogFileModifyUpdateNamed(hog_file, filename, data, len, timestamp, NULL);
		}
	} else {
		printf("Error opening hog file: %d\n", ret);
	}
	hogFileDestroy(hog_file, true);
	return ret;
}

void addFile(const char *path) {
    struct _finddata32_t c_file;
	intptr_t hFile;
	char local_base_folder[MAX_PATH];

	if( (hFile = findfirst32_SAFE( path, &c_file )) == -1L ) {
		printf( "No files matching '%s found!\n", path);
	} else {
		if (path[0]!='.' && path[1]!=':' && path[0]!='/') {
			// relative patch
			sprintf(local_base_folder, "./%s", path);
		} else {
			strcpy(local_base_folder, path);
		}
		forwardSlashes(local_base_folder);
		getDirectoryName(local_base_folder);
		if (base_folder_set) {
			assert(strnicmp(local_base_folder, base_folder, strlen(base_folder))==0);
		} else {
			strcpy(base_folder, local_base_folder);
		}
		do {
			pigInputProcessor(local_base_folder, &c_file, NULL);
		} while (findnext32_SAFE( hFile, &c_file ) == 0 );
		_findclose( hFile);
		printf("\n");
	}
}

void parseFileSpec(const char *filespec)
{
	char *s,*mem,*args[100];
	int count;
	char temp[MAX_PATH];
	const char *filename;
	// read a bunch of wildcards from a file

	eaDestroyEx(&filespecs, NULL);

	if (!filespec)
		return;

	if (strncmp(filespec, "-T", 2)==0)
	{
		// reference to a file
		filename = filespec + 2;
	} else {
		// Just one spec
		eaPush(&filespecs, strdup(filespec));
		return;
	}

	strcpy(temp, filename);
	mem = extractFromFS(temp, 0);
	if (!mem) {
		sprintf(temp, "./%s", filename);
		mem = extractFromFS(temp, 0);
	}
	if (!mem) {
		printf("Error opening list file: %s!\n", filename);
		return;
	}

	// Fill data
	mem = extractFromFS(temp, 0);
	s = mem;
	while(s && (count=tokenize_line(s,args,&s))>=0)
	{
		char *c;
		char *spec;
		if (count == 0 || args[0][0]=='#')
			continue;

		if (count == 1) {
			spec = strdup(args[0]);
		} else{
			char buf[2048]="";
			int i;
			for (i=0; i<count; i++) {
				strcat(buf, args[i]);
				if (i!=count-1)
					strcat(buf, " ");
			}
			spec = strdup(buf);
		}

		for (c=spec; *c; c++ )
			*c = toupper(*c);
		eaPush(&filespecs, spec);
	}
	free(mem);
}

void addFromFile(const char *filespec)
{
	parseFileSpec(filespec);

	base_folder_set=1;
	strcpy(base_folder, ".");
	fileScanDirRecurseEx(base_folder, &pigInputProcessor, NULL);
}

StashTable prune_table = 0;
void buildPruneTable(const NewPigEntry *entries, int count)
{
	int i;
	if (prune_table)
		stashTableDestroy(prune_table);
	prune_table = stashTableCreateWithStringKeys(count*2, StashDefault);
	for (i=0; i<count; i++) {
		stashAddInt(prune_table, entries[i].fname, 1, false);
	}
}

bool shouldPrune(const NewPigEntry *entries, int count, const char *fn)
{
	if (!stashFindElement(prune_table, fn, NULL))
	{
		// Not in source list
		if (simpleFileSpecExcludesFile(fn, g_mirrorFileSpec))
			return false; // But excluded from mirroring, so would not be found on disk anyway
		return true;
	}
	return false;
}

void getData(NewPigEntry *entry)
{
	char fn[MAX_PATH];
	if (entry->data == (void*)1)
		return;
	sprintf(fn, "%s/%s", base_folder, entry->fname);
	assert(fileExists(fn));
	entry->data = extractFromFS(fn,&entry->size);
}

typedef struct ChangeLog {
	S64 delta;
	int count;
	int min;
	int max;
} ChangeLog;

static StashTable changes;

static void logChange(const char *ext, int sizedelta)
{
	ChangeLog *log;
	if (!changes) {
		changes = stashTableCreateWithStringKeys(256, StashDeepCopyKeys_NeverRelease);
	}
	if (!stashFindPointer(changes, ext, &log)) {
		log = calloc(sizeof(*log), 1);
		log->min = log->max = sizedelta;
		stashAddPointer(changes, ext, log, true);
	}
	log->delta += sizedelta;
	log->count++;
	log->min = MIN(log->min, sizedelta);
	log->max = MAX(log->max, sizedelta);
}

static int logChangeDumper(StashElement element)
{
	const char *ext = stashElementGetStringKey(element);
	ChangeLog *log = stashElementGetPointer(element);
	F32 delta = log->delta;
	F32 avg = delta / log->count;
	if (log->count < 10)
		return 1;
	printf("%10s avg: %12.5f  #changes: %6d min: %9d  max: %9d\n", ext, avg, log->count, log->min, log->max);
	return 1;
}

static void logChangeDump()
{
	stashForEachElement(changes, logChangeDumper);
}

// Updates a hogg file, removing missing files, adding new files, updating changed files
int updateHogFile(NewPigEntry *entries, int count, const char *output, bool prune)
{
	int timer = timerAlloc();
	int ret=0;
	U32 i;
	U32 numfiles;
	int lastval=0;
	HogFile *hog_file;
	bool bCreated;
	int delcount=0, modcount=0, newcount=0, timestampcount=0;
	if (verbose!=2)
		loadstart_printf("Modifying Hogg file...");
	if (verbose==2)
		loadstart_printf("Loading old hogg...");
	if (!(hog_file=hogFileReadEx(output, &bCreated, PIGERR_ASSERT, NULL, global_flags, default_journal_size)))
	{
		if (fileExists(output)) {
			assert(0); // Need better error recovery code!
		} else {
			assert(0); // Need to implement deciding to create anew if an update failed to read
		}
		hogFileDestroy(hog_file, true);
		loadend_printf("ERROR");
		return ret;
	}
	if (verbose==2)
		loadend_printf("done.");
	if (verbose==2)
		loadstart_printf("Pruning removed files...");
	// Prune removed files (do first to free space)
	buildPruneTable(entries, count);
	numfiles = hogFileGetNumFiles(hog_file);

	timerStart(timer);
	for (i=0; i<numfiles; i++) {
		const char *name = hogFileGetFileName(hog_file, i);
		if (!name)
			continue;
		if (hogFileIsSpecialFile(hog_file, i))
			continue;
		if (prune && shouldPrune(entries, count, name)) {
			if (verbose==2)
				printf("%s: REMOVED                         \n", name);
			delcount++;
			if (ret=hogFileModifyUpdateNamedSync(hog_file, name, NULL, 0, 0, NULL))
				assert(0);
		}
		if ((i % 100)==99 || delcount != lastval) {
			if (timerElapsed(timer) > 1) 
			{
				int v = printf("%d/%d (%d deleted)", i+1, numfiles, delcount);
#ifdef _XBOX
				printf("\n");
#else
				for (; v; v--) 
					printf("%c", 8); // Backspace
#endif
				lastval = delcount;
				timerStart(timer);
			}
		}
	}
	timerFree(timer);

	if (verbose==2)
		loadend_printf("done (%d deleted).", delcount);
	if (verbose==2)
		loadstart_printf("Modifying/Adding files...\n");
	// Modify existing files
	for (i=0; i<(U32)count; i++) {
		HogFileIndex file_index;
		const char *name = entries[i].fname;
		if ((file_index = hogFileFind(hog_file, name))!=HOG_INVALID_INDEX) {
			U32 timestamp = hogFileGetFileTimestamp(hog_file, file_index);
			U32 size = hogFileGetFileSize(hog_file, file_index);
			U32 headersize;
			const U8 *headerdata = hogFileGetHeaderData(hog_file, file_index, &headersize);
			bool needHeaderUpdate = !pigShouldCacheHeaderData(strrchr(name, '.')) != !headersize;
			bool isCompressed = hogFileIsZipped(hog_file, file_index);
			bool needUncompressing = pigShouldBeUncompressed(strrchr(name, '.')) && isCompressed;
			if (ABS_UNS_DIFF(timestamp, entries[i].timestamp) == 3600 &&
				gimmeTimeToUTC(timestamp) == entries[i].timestamp &&
				size == entries[i].size &&
				!needHeaderUpdate)
			{
				// gimmeTime -> UTC time conversion
				hogFileModifyUpdateTimestamp(hog_file, file_index, entries[i].timestamp);
				timestampcount++;
			} else if (timestamp != entries[i].timestamp ||
				size != entries[i].size ||
				needHeaderUpdate ||
				needUncompressing
				)
			{
				if (verbose==2)
					printf("%s: Modified                            \n", name);
				//logChange(strrchr(name, '.'), entries[i].size - size);
				modcount++;
				getData(&entries[i]);
				// Zips and crcs and gets headerdata in here:
				if (ret=hogFileModifyUpdateNamed2(hog_file, &entries[i]))
					assert(0);
				entries[i].data = NULL;
			} else {
				//printf("%s: Up to date\n", name);
			}
		} else {
			if (verbose==2)
				printf("%s: NEW                                   \n", name);
			newcount++;
			getData(&entries[i]);
			if (!entries[i].data) {
				Errorf("Error reading from file: %s", entries[i].fname);
			} else {
				if (ret=hogFileModifyUpdateNamedSync2(hog_file, &entries[i]))
					assert(0);
			}
			entries[i].data = NULL;
		}
		if ((i % 100)==99 || // We've checked 100 files or
			(modcount + newcount!=lastval) && (modcount+newcount<200)) // we just updated/modified a file, and there's a small total
		{
#ifndef _XBOX
			CONSOLE_SCREEN_BUFFER_INFO screenInfo = {0};
			GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &screenInfo);
			if (screenInfo.dwCursorPosition.X != 0)
				printf("\n");
#endif
			printf("%d/%d (%d modified, %d new)", i+1, count, modcount, newcount);
#ifdef _XBOX
			printf("\n");
#else
			printf("\r");
			GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &screenInfo);
			assert(screenInfo.dwCursorPosition.X == 0);
#endif
			lastval = modcount + newcount;
		}
	}
	if (verbose==2)
		loadend_printf("done (%d modified, %d new).", modcount, newcount);
	if (verbose==2)
		loadstart_printf("Flushing to disk and cleanup up...");
	hogFileModifyFlush(hog_file);
	hogFileDestroy(hog_file, true);
	if (verbose==2)
		loadend_printf("done.");
	if (verbose!=2) {
		if (timestampcount) {
			loadend_printf("done (%d removed, %d modified, %d new, %d timestamps fixed).", delcount, modcount, newcount, timestampcount);
		} else {
			loadend_printf("done (%d removed, %d modified, %d new).", delcount, modcount, newcount);
		}
	}
	//logChangeDump();
	return 0;
}

// Creates a .pig file out of all files in a specified folder
int makePig(const char *foldersource, const char *output, bool prune, bool useFolderName) {
	int timer,i,ret;

	timer = timerAlloc();
	timerStart(timer);

	// Init
	pig_entry_count = 0;
	strcpy(base_folder, foldersource);
	if (preserve_pathname && strStartsWith(base_folder, "./")) {
		strcpy(base_folder, "./");
	}
	forwardSlashes(base_folder);
	fixDoubleSlashes(base_folder);
	// Scan
	if (verbose)
		loadstart_printf("Scanning for files...");
	if (g_fileListSourceName) {
		char *s;
		int len;
		char *mem = fileAlloc(g_fileListSourceName, &len);
		assert(mem);
		s = mem;
		while (s < mem + len)
		{
			int slen;
			char buf[MAX_PATH] = {0};
			NewPigEntry *entry;
			entry = dynArrayAdd(pig_entries,sizeof(pig_entries[0]),pig_entry_count,pig_entry_max,1);
			slen = endianSwapIfBig(U32, *((U32*)s)); s+=sizeof(U32);
			memcpy(buf, s, slen); s+=slen;
			entry->fname = strdup(buf);
			entry->timestamp = endianSwapIfBig(U32, *((U32*)s)); s+=sizeof(U32);
			entry->size = endianSwapIfBig(U32, *((U32*)s)); s+=sizeof(U32);
		}
#ifndef _XBOX
		strcpy(base_folder, ".");
#else
		strcpy(base_folder, g_fileListSourceName);
		forwardSlashes(base_folder);
		s = strstri(base_folder, "/data/");
		assert(s);
		s += strlen("/data");
		*s = '\0';
#endif
	} else if (strncmp(base_folder, "-T", 2)==0) {
		// load in a list from file
		addFromFile(base_folder);
	} else if (dirExists(base_folder)) {
		fileScanDirRecurseEx(foldersource, &pigInputProcessor, NULL);
	} else if (useFolderName) {
		addPig(foldersource, output);
	} else {
		// filespec or single file - HACK
		addFile(base_folder);
	}

	if (verbose)
		loadend_printf("done.");

	if (output && !g_fileListDump) {
		// Incremental Update
		ret = updateHogFile(pig_entries, pig_entry_count, output, prune);
	} else {
		ret = 0; // genlist
	}
	for(i=0;i<pig_entry_count;i++)
	{
		if (!output) { // GEN_LIST
			printf("%s\n", pig_entries[i].fname);
		}
		SAFE_FREE(pig_entries[i].data);
		SAFE_FREE((char*)pig_entries[i].fname);
	}
	SAFE_FREE(pig_entries);
	pig_entry_count = pig_entry_max = 0;
	return ret;
}

static HogFile *g_extract_hog;
static int cmpExtractFile(const int *a, const int *b)
{
	U64 offa = hogFileGetOffset(g_extract_hog, *a);
	U64 offb = hogFileGetOffset(g_extract_hog, *b);
	return (offa > offb)?1:-1;
}

static int extractPig(const char *out, const char *filespec)
{
	int timer = timerAlloc();
	int timerShort = timerAlloc();
	int ret=0;
	U64 bytes_extracted=0;
	U64 bytes_extracted_short=0;
	U32 *files=NULL;
	HogFile *hog_file;

	parseFileSpec(filespec);

	loadstart_printf("Initializing extraction...");
	hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|global_flags);
	if (!hog_file)
		hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|HOG_READONLY|global_flags);
	if (!hog_file)
	{
		loadend_printf(" done.");
		printf("Unable to open %s.\n", out);
		ret = -1;
		goto fail;
	}

	eaiSetCapacity(&files, hogFileGetNumFiles(hog_file));

	if (hog_file )
	{
		FILE *f;
		U32 i;
		U32 j;
		U32 num_files;
		char buf[1024];
		char buf2[64];

		for (i=0; i<hogFileGetNumFiles(hog_file); i++) {
			const char *name = hogFileGetFileName(hog_file, i);

			if (!name)
				continue; // Unused file
			if (hogFileIsSpecialFile(hog_file, i))
				continue;
			if (filespec)
			{
				if (!checkFileSpec(name))
					continue;
			}
			eaiPush(&files, i);
		}

		g_extract_hog = hog_file;
		eaiQSort(files, cmpExtractFile);
		g_extract_hog = NULL;

		loadend_printf(" done.");
		loadstart_printf("Extracting files...");

		timerStart(timer);
		timerStart(timerShort);

		num_files = eaiSize(&files);
		for (j=0; j<num_files; j++) {
			const char *name;
			char filtered_name[MAX_PATH];
			U32 count;
			char *mem;
			PERFINFO_AUTO_START("top", 1);

			i = files[j];
			name = hogFileGetFileName(hog_file, i);

			if (timerElapsed(timerShort) > 1.f || i == hogFileGetNumFiles(hog_file)-1)
			{
				F32 elapsed = timerElapsed(timer);
				F32 elapsed_short = timerElapsed(timerShort);
				if (!elapsed)
					elapsed = elapsed_short = 1;
				sprintf(buf, "Extracting... %d/%d files, %1.1fs, %s/s, %s/s average",
					j+1, num_files,
					timerElapsed(timer),
					friendlyBytes(bytes_extracted_short / elapsed_short),
					friendlyBytesBuf(bytes_extracted / elapsed, buf2));
				OutputDebugStringf("%s\n", buf);
				setConsoleTitle(buf);
				bytes_extracted_short = 0;
				timerStart(timerShort);
			}

			PERFINFO_AUTO_STOP_START("extract", 1);
			mem = hogFileExtract(hog_file, i, &count, NULL);
			if (!mem) {
				printf("Error getting %s from archive!\n", name);
				ret = 1;
				goto fail;
			}
			if (verbose) {
				printf("%s\n", name);
			}
			strcpy(filtered_name, name);
			strchrReplace(filtered_name, '?', '_');
			PERFINFO_AUTO_STOP_START("mkdirtree", 1);
			mkdirtree((char*)filtered_name);
			PERFINFO_AUTO_STOP_START("fopen", 1);
			f = fopen(filtered_name, "wb");
			if (!f) {
				printf("Can't open %s for writing!\n", filtered_name);
				ret = 1;
				goto fail;
			}
			PERFINFO_AUTO_STOP_START("fwrite", 1);
			fwrite(mem, 1, count, f);
			PERFINFO_AUTO_STOP_START("fclose", 1);
			fclose(f);
			PERFINFO_AUTO_STOP_START("bottom", 1);
			{
				//struct _utimbuf utb;
				//utb.actime = utb.modtime = hogFileGetFileTimestamp(hog_file, i);
				//_utime(name, &utb);
				PERFINFO_AUTO_START("_SetUTCFileTimesCMA", 1);
				_SetUTCFileTimesCMA(name, 0, hogFileGetFileTimestamp(hog_file, i), hogFileGetFileTimestamp(hog_file, i));
			}
			PERFINFO_AUTO_STOP_START("free", 1);
			free(mem);
			PERFINFO_AUTO_STOP();
			bytes_extracted += count;
			bytes_extracted_short += count;
			PERFINFO_AUTO_STOP();
		}
		loadend_printf(" done (%d files, %s, %s/s).", num_files, friendlyBytes(bytes_extracted), friendlyBytesBuf(bytes_extracted / AVOID_DIV_0(timerElapsed(timer)), buf));
	}
fail:
	eaiDestroy(&files);
	hogFileDestroy(hog_file, true);
	timerFree(timer);
	timerFree(timerShort);
	return ret;
}

static int deleteSingleFile(HogFile *hog_file, const char *filespec, bool is_random, int random_percent)
{
	int ret=0;
	U32 i;
	int num_deleted=0;
	for (i=0; i<hogFileGetNumFiles(hog_file); i++) {
		const char *name = hogFileGetFileName(hog_file, i);
		int r2;
		if (!name)
			continue; // Unused file
		if (hogFileIsSpecialFile(hog_file, i))
			continue;
		if (is_random)
		{
			if (randInt(100) >= random_percent)
				continue;
		} else {
			if (!checkFileSpec(name))
				continue;
		}
		if (verbose) {
			printf("%s\n", name);
		}
		num_deleted++;
		r2 = hogFileModifyDeleteNamed(hog_file, name);
		if (r2) {
			printf("Error modifying hog file: %d\n", r2);
			ret = r2;
		}
	}
	printf("%d file%s deleted.\n", num_deleted, (num_deleted==1)?"":"s");
	return ret;
}

static int deleteSingleFileByName(HogFile *hog_file, const char *filename)
{
	int ret=0;
	int r2;
	HogFileIndex index = hogFileFind(hog_file, filename);
	if(index == HOG_INVALID_INDEX)
	{
		printf("%s,Not in hogg\n", filename);
	}
	else
	{
		r2 = hogFileModifyDeleteNamed(hog_file, filename);
		if (r2) {
			printf("Error modifying hog file: %d\n", r2);
			ret = r2;
		}
	}
	return ret;
}

static int deletePig(const char *out, const char *filespec)
{
	int ret=0;
	// Special syntax for deleting a random percentage of files: e.g. pig df out.hogg __random__90
	bool is_random = strStartsWith(filespec, "__random__");
	int random_percent = is_random?(atoi(filespec + strlen("__random__"))):0;
	HogFile *hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|HOG_MUST_BE_WRITABLE|global_flags);

	parseFileSpec(filespec);

	if (hog_file )
	{
		ret = deleteSingleFile(hog_file, filespec, is_random, random_percent);
	} else {
		printf("Error opening hog file: %d\n", ret);
	}
	hogFileDestroy(hog_file, true);
	return ret;
}

static int deleteMassPig(const char *out, const char *filename)
{
	int ret=0;
	// Special syntax for deleting a random percentage of files: e.g. pig df out.hogg __random__90
	HogFile *hog_file;
	int num_deleted=0;

	loadstart_printf("Opening source (%s)...", out);
	hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|HOG_MUST_BE_WRITABLE|global_flags);
	loadend_printf("done.");

	if (hog_file )
	{
		int i;
		char *newstring = NULL;
		int length = 0;
		char **filenamelist = NULL;
		loadstart_printf("Loading list of files to delete (%s)...", filename);
		newstring = fileAlloc(filename, &length);
		DivideString(newstring, "\n", &filenamelist, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
		for(i = 0; i < eaSize(&filenamelist); ++i)
		{
			if(stricmp(filenamelist[i], HOG_DATALIST_FILENAME) == 0)
			{
				printf("Trying to delete the DataList. No deletes have been made. Aborting\n");
				fileFree(newstring);
				eaDestroyEx(&filenamelist, NULL);
				hogFileDestroy(hog_file, true);
				loadend_printf("done.");
				return 0;
			}
		}
		loadend_printf("done.");
		loadstart_printf("Performing deletes...");
		printf("\n");
		for(i = 0; i < eaSize(&filenamelist); ++i)
		{
			if(i%1000 == 0)
				printf("Deleted %d/%d.\r", i, eaSize(&filenamelist));

			ret = deleteSingleFileByName(hog_file, filenamelist[i]);
		}
		loadend_printf("done.\n");
		fileFree(newstring);
		eaDestroyEx(&filenamelist, NULL);
	} else {
		printf("Error opening hog file: %d\n", ret);
	}
	hogFileDestroy(hog_file, true);
	return ret;
}

static void pigSymStore(const char *store, const char *hogname)
{
	int ret=0;
	HogFile *hog_file = hogFileRead(hogname, NULL, PIGERR_ASSERT, &ret, HOG_NOCREATE|HOG_READONLY|global_flags);
	assert(hog_file);

	if (hog_file) {
		unsigned int i;
		for (i=0; i<hogFileGetNumFiles(hog_file); i++) 
		{
			const char *name;
			char shortname[MAX_PATH];
			char tempname[MAX_PATH];
			if (!(name=hogFileGetFileName(hog_file, i)))
				continue;
			if (hogFileIsSpecialFile(hog_file, i))
				continue;
			strcpy(shortname, getFileNameConst(name));
			if (strchr(shortname, '.')) {
				// Actual filename
			} else {
				char buf[MAX_PATH];
				char *s;
				strcpy(buf, name);
				s = strrchr(buf, '/');
				if (s) {
					*s = '\0';
					s = strrchr(buf, '/');
				}
				if (s)
				{
					strcpy(shortname, s+1);
				} else {
					continue;
				}
			}
			if (strEndsWith(shortname, ".pdb") ||
				strEndsWith(shortname, ".exe") ||
				strEndsWith(shortname, ".dll") ||
				strEndsWith(shortname, ".dle") ||
				strEndsWith(shortname, ".dli") ||
				strEndsWith(shortname, ".xex") ||
				strEndsWith(shortname, ".intermediate"))
			{
				U8 *data;
				U32 filesize;
				printf("%s (%s) :\n", shortname, name);
				sprintf(tempname, "c:/temp/%s", shortname);
				data = hogFileExtract(hog_file, i, &filesize, NULL);
				if (data) {
					FILE *fout;
					int wrote;
					char cmd[MAX_PATH*2];
					ret = mkdir("c:\\temp");
					chmod(tempname, _S_IREAD | _S_IWRITE);
					fout = fopen(tempname, "wb");
					assert(fout);
					wrote = (int)fwrite(data, 1, filesize, fout);
					fclose(fout);
					assert(wrote == filesize);
					sprintf(cmd, "CrypticSymstore %s add %s", store, tempname);
					ret = system(cmd);
					if (ret != 0) {
						printf("\n\nSymStore failed!\n\n");
						//pak();
					}
					ret = unlink(tempname);
					free(data);
				}
			}
		}
		
		hogFileDestroy(hog_file, true);
	}
}

static int listPig(const char *out)
{
	int ret=0;
	HogFile *hog_file;
	
	if (repair_on_first_open)
	{
		hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|global_flags);
	} else {
		hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|HOG_NO_REPAIR|global_flags);
		if (!hog_file && fileExists(out))
		{
			printf("Unable to open hogg file, may be corrupted, do you want to try to repair it?");
			if (consoleYesNo())
			{
				hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|global_flags);
				if (!hog_file)
					printf("Unable to open hogg file even with repairing.\n");
				else
					printf("Successfully opened hogg file.\n");
			}
		}

		if (!hog_file)
			hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|HOG_READONLY|global_flags);
	}

	if (hog_file) {
		hogFileDumpInfo(hog_file, verbose, debug_verbose);
		hogFileDestroy(hog_file, true);
	} else {
		if (!fileExists(out))
			printf("Unable to open hogg file: file not found\n");
		else
			printf("Unable to open hogg file: error %d\n", ret);
	}
	return ret;
}

static int verifyPig(const char *out)
{
	int ret=0;
	HogFile *hog_file;
	
	if (repair_on_first_open)
	{
		hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|global_flags);
	} else {
		hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|HOG_NO_REPAIR|global_flags);
		if (!hog_file && fileExists(out))
		{
			printf("Unable to open hogg file, may be corrupted, do you want to try to repair it?");
			if (consoleYesNo())
			{
				hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|global_flags);
				if (!hog_file)
					printf("Unable to open hogg file even with repairing.\n");
				else
					printf("Successfully opened hogg file.\n");
			}
		}

		if (!hog_file)
			hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|HOG_READONLY|global_flags);
	}

	if (hog_file) {
		char *str=NULL;
		bool bGood = hogFileVerifyToEstr(hog_file, &str, fix_on_first_verify?true:false);
		if (fix_on_first_verify)
		{
			// strtok it so it shows up on Xbox
			char *s, *context=NULL, *s2=str;
			printf("Verify results:\n");
			while (s = strtok_s(s2, "\n", &context))
			{
				s2 = NULL;
				printf("%s\n", s);
			}
		}
		else if (!bGood)
		{
			// strtok it so it shows up on Xbox
			char *s, *context=NULL, *s2=str;
			printf("Verify FAILED:\n");
			while (s = strtok_s(s2, "\n", &context))
			{
				s2 = NULL;
				printf("%s\n", s);
			}
			printf("\nWould you like to delete the corrupt files?");
			if (consoleYesNo())
			{
				hogFileVerifyToEstr(hog_file, &str, true);
			}
		} else {
			printf("Verify PASSED\n%s", str);
		}
		estrDestroy(&str);
		hogFileDestroy(hog_file, true);
	}
	return ret;
}

AUTO_STRUCT;
typedef struct PigEntryInfo
{
	const char *filename;	AST( NAME(Filename) POOL_STRING FORMAT_LVWIDTH(250) )
	int size;				AST( NAME("File Size") )
	int packed_size;		AST( NAME("Compressed Size") )
	__time32_t timestamp;	AST( NAME("Modification Time") FORMAT_FRIENDLYDATE FORMAT_LVWIDTH(120) )
	int header_size;		AST( NAME("Cached Header Size") )
	U64 file_offset;		AST( NAME("Offset") )
	int index;				AST( NAME("FileIndex") )
	int ea_id;				AST( NAME("EAIndex") )
	U32 checksum;			AST( NAME("Checksum") )
} PigEntryInfo;

static PigEntryInfo **eaFiles=NULL;
static HogFile *pig_view_hog_file;
#ifndef _XBOX
static ListView *lvFiles;
static HWND g_hDlg;
static HWND hStatusBar;

void pigViewRemove(ListView *lv, PigEntryInfo *entry, void *data)
{
	static bool didit=false;
	hogFileModifyDeleteNamed(pig_view_hog_file, entry->filename);
	// TODO: refresh
	if (!didit)
		MessageBox(NULL, L"File removed, close and re-open to refresh view", L"File removed", MB_OK);
	didit = true;
}

void pigViewView(ListView *lv, PigEntryInfo *entry, void *data)
{
	void *filedata;
	U32 count;
	filedata = hogFileExtract(pig_view_hog_file, entry->index, &count, NULL);
	if (filedata) {
		FILE *f;
		char tempname[MAX_PATH];

		sprintf(tempname, "C:/temp/%s", getFileNameConst(entry->filename));
		strchrReplace(tempname, '?', '_');
		mkdirtree(tempname);
		f = fileOpen(tempname, "wb");
		fwrite(filedata, 1, count, f);
		fileClose(f);
		free(filedata);
		fileOpenWithEditor(tempname);
	}
}

static int g_extract_count;
static bool g_extract_zipped = false;
void pigViewExtractInternal(ListView *lv, PigEntryInfo *entry, void *data, bool bShowMessage)
{
	void *filedata;
	U32 count;
	if (g_extract_zipped)
		filedata = hogFileExtractCompressed(pig_view_hog_file, entry->index, &count);
	else
		filedata = hogFileExtract(pig_view_hog_file, entry->index, &count, NULL);
	if (filedata) {
		FILE *f;
		char tempname[MAX_PATH];
		char fullpath[MAX_PATH];
		char message[1000];

		makefullpath(entry->filename, fullpath);
		//winGetFileName_s(g_hwnd, "*.*", 
		sprintf(tempname, "./%s", entry->filename);
		if (g_extract_zipped)
		{
			strcat(tempname, ".z");
			strcat(fullpath, ".z");
		}
		strchrReplace(tempname, '?', '_');
		strchrReplace(fullpath, '?', '_');
		mkdirtree(tempname);
		f = fileOpen(tempname, "wb");
		if (f) {
			fwrite(filedata, 1, count, f);
			fileClose(f);
			free(filedata);

			if (bShowMessage)
			{
				sprintf(message, "File extracted to %s", fullpath);
				MessageBox_UTF8(NULL, message, "File extracted", MB_OK);
			} else {
				g_extract_count++;
			}
		} else {
			sprintf(message, "Failed to open %s for writing", fullpath);
			MessageBox_UTF8(NULL, message, "Extract failed", MB_OK);
		}
	}
}

void pigViewExtract(ListView *lv, PigEntryInfo *entry, void *data)
{
	pigViewExtractInternal(lv, entry, data, true);
}

void pigViewExtractMultiple(ListView *lv, PigEntryInfo *entry, void *data)
{
	pigViewExtractInternal(lv, entry, data, false);
}

void pigViewAdd(void)
{
}

void pigViewArchiveInfo(void)
{
	char *str=NULL;
	hogFileDumpInfoToEstr(pig_view_hog_file, &str);
	MessageBox_UTF8(g_hDlg, str, "Archive Info", MB_OK);
	estrDestroy(&str);
}

void pigViewVerify(void)
{
	char *str=NULL;
	bool bGood = hogFileVerifyToEstr(pig_view_hog_file, &str, fix_on_first_verify?true:false);
	if (fix_on_first_verify)
	{
		MessageBox_UTF8(g_hDlg, str, "Verify Results", MB_OK);
	}
	else if (!bGood)
	{
		printf("%s\n", str);
		estrConcatf(&str, "\n\nWould you like to delete the corrupt files?");
		if (IDYES == MessageBox_UTF8(g_hDlg, str, "Verify FAILED", MB_YESNO))
		{
			hogFileVerifyToEstr(pig_view_hog_file, &str, true);
		}
	} else {
		MessageBox_UTF8(g_hDlg, str, "Verify PASSED", MB_OK);
	}
	estrDestroy(&str);
}

void setStatusBar(int elem, FORMAT_STR const char *fmt, ...)
{
	char str[1000];
	va_list ap;

	va_start(ap, fmt);
	vsprintf(str, fmt, ap);
	va_end(ap);

	SendMessage(hStatusBar, SB_SETTEXT, elem, (LPARAM)str);
}

bool lvDoingInitialBuildOrExiting=false;
bool bNeedStatusUpdate=false;

static void doStatusUpdate(HWND hDlg)
{
	// Count number of selected elements
	int i = listViewDoOnSelected(lvFiles, NULL, NULL);
	if (i>=1) {
		EnableWindow(GetDlgItem(hDlg, IDC_REMOVE), TRUE);
		EnableWindow(GetDlgItem(hDlg, IDC_EXTRACT), TRUE);
	} else {
		EnableWindow(GetDlgItem(hDlg, IDC_REMOVE), FALSE);
		EnableWindow(GetDlgItem(hDlg, IDC_EXTRACT), FALSE);
	}

	if (i==1) {
		EnableWindow(GetDlgItem(hDlg, IDC_VIEW), TRUE);
	} else {
		EnableWindow(GetDlgItem(hDlg, IDC_VIEW), FALSE);
	}

	setStatusBar(0, "%d files selected", i); // TODO: More fun info here!
}

LRESULT CALLBACK DlgPigViewProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	int i;
	RECT rect;

	g_hDlg = hDlg;

	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			char buf[1024];
			int addTimer = timerAlloc();
			int printfTimer = timerAlloc();
			int totalTimer = timerAlloc();
			int status_at=1000;
			int last_status_at=0;
			int last_percentage=0;
			F32 adds_per_second=10000; // Default timing value
			int count;
			sprintf(buf, "%s - Hogg Viewer", hogFileGetArchiveFileName(pig_view_hog_file));
			SetWindowText_UTF8(hDlg, buf);

			// File List
			lvDoingInitialBuildOrExiting = true;
			listViewInit(lvFiles, parse_PigEntryInfo, hDlg, GetDlgItem(hDlg, IDC_FILELIST));
			listViewDoingInitialBuild(lvFiles, true);
			count = eaSize(&eaFiles);
			printf("Creating Win32 UI (%d elements)...\n", count);
			for (i=0; i<count; i++) {
				listViewAddItem(lvFiles, eaGet(&eaFiles, i));
				if (i == status_at)
				{
					adds_per_second = (i-last_status_at) / timerElapsed(addTimer);
					//printf("@%d: %f adds/second\n", i, adds_per_second);
					last_status_at = status_at;
					if (status_at < 100000)
						status_at *= 10;
					else
						status_at += 100000;
					timerStart(addTimer);
				}
				{
					int percentage = (i+1) * 100/ count;
					if (percentage != last_percentage && timerElapsed(printfTimer)>0.1 ||
						timerElapsed(printfTimer)>1 || 
						percentage == 100)
					{
						int curTime = (int)timerElapsed(totalTimer);
						int endTime = (int)(timerElapsed(totalTimer) + (count - i - 1) / (0.9*adds_per_second));
						printf("\r% 3d%% (%d:%02d / %d:%02d)  ", percentage, curTime/60, curTime%60, endTime/60, endTime%60);
						last_percentage = percentage;
						timerStart(printfTimer);
					}
				}
			}
			printf("done.\n");
			listViewSort(lvFiles, 0); // Filename
			listViewDoingInitialBuild(lvFiles, false);
			lvDoingInitialBuildOrExiting = false;

			timerFree(addTimer);
			timerFree(printfTimer);
			timerFree(totalTimer);

			// Status bar stuff
			hStatusBar = CreateStatusWindow( WS_CHILD | WS_VISIBLE, L"Status bar", hDlg, 1);
			{
				int temp[1];
				temp[0]=-1;
				SendMessage(hStatusBar, SB_SETPARTS, ARRAY_SIZE(temp), (LPARAM)temp);
			}

			// TODO: enable button when Add works
			EnableWindow(GetDlgItem(hDlg, IDC_ADD), FALSE);

			EnableWindow(GetDlgItem(hDlg, IDC_EXTRACT), FALSE);
			EnableWindow(GetDlgItem(hDlg, IDC_VIEW), FALSE);
			EnableWindow(GetDlgItem(hDlg, IDC_REMOVE), FALSE);

			GetClientRect(hDlg, &rect); 
			doDialogOnResize(hDlg, (WORD)(rect.right - rect.left), (WORD)(rect.bottom - rect.top), IDC_ALIGNME, IDC_UPPERLEFT);

			SetTimer(hDlg, 0, 100, NULL);

			return FALSE;
		}
	case WM_SIZE:
		{
			WORD w = LOWORD(lParam);
			WORD h = HIWORD(lParam);
			doDialogOnResize(hDlg, w, h, IDC_ALIGNME, IDC_UPPERLEFT);
			SendMessage(hStatusBar, iMsg, wParam, lParam);
			break;
		}
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
			case IDOK:
			case IDCANCEL:
				lvDoingInitialBuildOrExiting = true;
				EndDialog(hDlg, 0);
				return TRUE;
			xcase IDC_REMOVE:
				listViewDoOnSelected(lvFiles, pigViewRemove, NULL);
			xcase IDC_VIEW:
				listViewDoOnSelected(lvFiles, pigViewView, NULL);
			xcase IDC_EXTRACT:
				g_extract_zipped = !!(GetAsyncKeyState(VK_SHIFT) & 0x8000000);
				if (listViewDoOnSelected(lvFiles, NULL, NULL) > 1)
				{
					char message[1000];
					g_extract_count = 0;
					listViewDoOnSelected(lvFiles, pigViewExtractMultiple, NULL);
					sprintf(message, "%d files extracted", g_extract_count);
					MessageBox_UTF8(NULL, message, "Files extracted", MB_OK);
				} else
					listViewDoOnSelected(lvFiles, pigViewExtract, NULL);
			xcase IDC_ADD:
				pigViewAdd();
			xcase IDC_ARCHIVE_INFO:
				pigViewArchiveInfo();
			xcase IDC_VERIFY:
				pigViewVerify();

		}
		break;

	case WM_NOTIFY:
		if (!lvDoingInitialBuildOrExiting)
		{
			int idCtrl = (int)wParam;
			listViewOnNotify(lvFiles, wParam, lParam, NULL);

			if (idCtrl == IDC_FILELIST) {
				if (!bNeedStatusUpdate)
				{
					doStatusUpdate(hDlg);
					bNeedStatusUpdate = true;
				}
			}
		}
		break;
	case WM_TIMER:
		{
			if (bNeedStatusUpdate)
			{
				doStatusUpdate(hDlg);
				bNeedStatusUpdate = false;
			}
			{
				static bool bLast = false;
				bool bNow = !!(GetAsyncKeyState(VK_SHIFT) & 0x8000000);
				if (bLast!=bNow)
					SetDlgItemText(hDlg, IDC_EXTRACT, bNow?L"Extract Zipped":L"Extract");
				bLast = bNow;
			}
		}
		break;
	}
	return FALSE;
}


static int win32listPig(const char *out)
{
	int ret=0;
	int i;

	HogFile *hog_file;
	
	printf("Loading hogg...");
	if (repair_on_first_open)
	{
		hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|global_flags);
	} else {
		hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|HOG_NO_REPAIR|global_flags);
		if (!hog_file && fileExists(out))
		{
			if (IDYES == MessageBox_UTF8(GetConsoleWindow(), "Unable to open hogg file, may be corrupted, do you with to try to repair it?", "Unable to open hogg file", MB_YESNO))
			{
				hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|global_flags);
				if (!hog_file)
					MessageBox_UTF8(GetConsoleWindow(), "Unable to open hogg file even with repairing.", "ERROR", MB_OK);
			}
		}

		if (!hog_file)
			hog_file = hogFileRead(out, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|HOG_READONLY|global_flags);
	}
	printf(" done.\n");

	pig_view_hog_file = hog_file;

	if (hog_file) {
		int hiret;
		PigEntryInfo *entries;

		if (hogFileGetNumFiles(hog_file) > 100000)
		{
			printfColor(COLOR_RED|COLOR_GREEN|COLOR_BRIGHT, "\nCreating a giant Windows UI is slow, you might consider running the command line version:\n"
				"  pig w file.hogg > list.txt\n"
				"  or for all info: pig wvv2 file.hogg > list.txt\n\n");
		}

		printf("Gathering info...");
		entries = calloc(sizeof(PigEntryInfo), hogFileGetNumFiles(hog_file));
		for (i=0; i<(int)hogFileGetNumFiles(hog_file); i++) 
		{
			if (!hogFileGetFileName(hog_file, i))
				continue;
			//if (!hogFileIsSpecialFile(hog_file, i))
			{
				PigEntryInfo *entry = &entries[i];
				entry->filename = hogFileGetFileName(hog_file, i);
				hogFileGetSizes(hog_file, i, &entry->size, &entry->packed_size);
				hogFileGetHeaderData(hog_file, i, &entry->header_size);
				entry->timestamp = hogFileGetFileTimestamp(hog_file, i);
				entry->index = i;
				entry->file_offset = hogFileGetOffset(hog_file, i);
				entry->ea_id = hogFileGetEAIDInternal(hog_file, i);
				entry->checksum = hogFileGetFileChecksum(hog_file, i);
				eaPush(&eaFiles, entry);
			}
		}
		printf(" done.\n");

		lvFiles = listViewCreate();
		listViewSetNoScrollToEndOnAdd(lvFiles, true);
		hiret = DialogBox (winGetHInstance(), (LPCTSTR) (IDD_PIG), NULL, (DLGPROC)DlgPigViewProc);
		//listViewDestroy(lvFiles);
		
		//eaDestroy(&eaFiles); // Not calling for performance reasons
		//free(entries); // Not calling for performance reasons
		//hogFileDestroy(hog_file, true); // Not calling for performance reasons
	} else {
		if (!fileExists(out))
			printf("Unable to open hogg file: file not found\n");
		else
			printf("Unable to open hogg file: error %d\n", ret);
	}
	return ret;
}
#endif

static int syncHogs(const char *outname, const char *srcfiles)
{
	int finalret=0;
	char out[MAX_PATH];
	if (strchr(srcfiles, '*'))
	{
		int ret=0;
		bool good=true;
		//HANDLE handle;
		U32 handle=0;
		WIN32_FIND_DATAA wfd;
		char base[MAX_PATH];
		char *s;
		char **bases=NULL;
		strcpy(base, srcfiles);
		forwardSlashes(base);
		if (s = strrchr(base, '/'))
			*s = '\0';
		else
			strcpy(base, "");
		// Wildcard, multiple files
		//for(handle = FindFirstFile(srcfiles, &wfd), good=true; handle!=INVALID_HANDLE_VALUE && good; good = FindNextFile(handle, &wfd))
		//{
		for(good = fwFindFirstFile(&handle, srcfiles, &wfd); good; good = fwFindNextFile(handle, &wfd))
		{
			char srcpath[MAX_PATH];
			char newbase[MAX_PATH];
			if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			if(stricmp(wfd.cFileName, ".") == 0 || stricmp(wfd.cFileName, "..") == 0)
				continue;
			if (!strEndsWith(wfd.cFileName, ".hogg"))
				continue;
			strcpy(srcpath, base);
			if (base[0])
				strcat(srcpath, "/");
			strcat(srcpath, wfd.cFileName);
			strcpy(out, outname);
			forwardSlashes(out);
			assert(!strchr(out, '.') || strrchr(out, '/') > strrchr(out, '.')); // Shouldn't have an extension, just a prefix
			wfd.cFileName[0] = toupper(wfd.cFileName[0]);
			strcat(out, wfd.cFileName);
			printf("\nSyncing %s to %s...\n", srcpath, out);
			ret = hogSync(out, srcpath, sync_max_size, global_flags, default_journal_size, defrag_flags);
			if (ret)
				finalret = ret;

			strcpy(newbase, getFileNameConst(outname));
			strcat(newbase, wfd.cFileName);
			s = strrchr(newbase, '.');
			assert(s && stricmp(s, ".hogg")==0);
			*s = '\0';
			s = strdup(newbase);
			eaPush(&bases, s);
		}
		//if (handle != INVALID_HANDLE_VALUE)
		//	FindClose(handle);
		fwFindClose(handle);

		if (!eaSize(&bases))
		{
			// No data!   Probably network error
			printf("Could not find any files in %s\n, skipping sync process, probably network error.\n",
				srcfiles);
			return 1;
		}
		
		// Prune orphaned hoggs
		strcpy(out, outname);
		strcat(out, "*.hogg");
		//for(handle = FindFirstFile(out, &wfd), good=true; handle!=INVALID_HANDLE_VALUE && good; good = FindNextFile(handle, &wfd))
		//{
		for(good = fwFindFirstFile(&handle, out, &wfd); good; good = fwFindNextFile(handle, &wfd))
		{
			if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			if(stricmp(wfd.cFileName, ".") == 0 || stricmp(wfd.cFileName, "..") == 0)
				continue;
			if (!strStartsWithAny(wfd.cFileName, bases))
			{
				char srcpath[MAX_PATH];
				printf("\nDeleting orphaned hogg file: %s\n", wfd.cFileName);
				strcpy(srcpath, outname);
				forwardSlashes(srcpath);
				if (s = strrchr(srcpath, '/'))
					*s = '\0';
				else
					strcpy(srcpath, "");
				strcat(srcpath, "/");
				strcat(srcpath, wfd.cFileName);
				fileForceRemove(srcpath);
			}
		}
		//if (handle != INVALID_HANDLE_VALUE)
		//	FindClose(handle);
		fwFindClose(handle);

		eaDestroyEx(&bases, NULL);
	} else {
		// Single source
		strcpy(out, outname);
		if (!strEndsWith(out, ".hogg"))
			strcat(out, ".hogg");
		finalret = hogSync(out, srcfiles, sync_max_size, global_flags, default_journal_size, defrag_flags);
	}
	if (shutdown_remote_server)
	{
		if (fileIsFileServerPath(srcfiles))
			fopen(srcfiles, "shutdown");
	}
	return finalret;
}

static int lockHog(const char *filename)
{
	int ret=0;

	HogFile *hog_file;

	printf("Loading hogg...");
	if (repair_on_first_open)
	{
		hog_file = hogFileRead(filename, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|global_flags);
	} else {
		hog_file = hogFileRead(filename, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|HOG_NO_REPAIR|global_flags);
		if (!hog_file && fileExists(filename))
		{
			if (IDYES == MessageBox_UTF8(GetConsoleWindow(), "Unable to open hogg file, may be corrupted, do you with to try to repair it?", "Unable to open hogg file", MB_YESNO))
			{
				hog_file = hogFileRead(filename, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|global_flags);
				if (!hog_file)
					MessageBox_UTF8(GetConsoleWindow(), "Unable to open hogg file even with repairing.", "ERROR", MB_OK);
			}
		}

		if (!hog_file)
			hog_file = hogFileRead(filename, NULL, PIGERR_PRINTF, &ret, HOG_NOCREATE|HOG_READONLY|global_flags);
	}

	if (!hog_file)
	{
		printf(" does not exist, ignoring lock request.");
		return -1;
	}
	printf(" done.\n");

	hogFileLock(hog_file);
	printf("Hogg %s locked, press any key to unlock...\n", filename);
	_getch();
	hogFileUnlock(hog_file);
	printf("Unlocked.\n");
	hogFileDestroy(hog_file, true);

	return 0;
}


static void nocrashCallback(ErrorMessage* errMsg, void *userdata)
{
	char *errString = errorFormatErrorMessage(errMsg);
	log_printf(0, "Shutting down from fatal error \"%s\"", errString);
	logWaitForQueueToEmpty();
	exit(1);
}

static void pigRegister(void)
{
#ifndef _XBOX
	winRegisterMeEx("open", ".hogg", "tv2p \"%1\"");
	winRegisterMeEx("Console View", ".hogg", "wv2p \"%1\"");
	winRegisterMeEx("Extract Here", ".hogg", "xvf \"%1\"");
#endif
}

// Called multiple times
int pigMainRun(int argc, char *argv[])
{
	char out[MAX_PATH], fn[MAX_PATH];
	int ret=0;
	char *valid_commands = "-cdtxg23adhlpuvyzfwsim";
	int mode=0;
	int i;
	int overrideout=0;

	if (argc<3 || !(strspn(argv[1], valid_commands)==strlen(argv[1])) || strcmp(argv[1], "--help")==0) {
		printf("usage: %s  (c|u|t|w|x|g|s|a|d|f|l|m)[flags][f out] [FILE]\n", argv[0]);
		printf(" modes (one required):\n");
		printf("  c    create a new Hogg file (deletes existing)\n");
		printf("  u    incrementally update or create a Hogg file\n");
		printf("  t    list the contents of name.hogg in a Windows GUI\n");
		printf("  v    verify hog contents (2 passes, second pass optionally fixes\n");
		printf("  w    list the contents of name.hogg in the old console style\n");
		printf("  x    extract the contents of name.hogg to the current folder\n");
		printf("          you may optionally specify a single file or filespec for FILE\n");
		printf("  g    generate list of files that would be hogged\n");
		printf("  s    sync a hogg set with a folder full of hoggs\n");
		printf("  a    add a file to a hogg (replaces existing)\n");
		printf("  d    delete a file or set of files from a hogg\n");
		printf("  f    defragment a hogg (by sync to temp file, not in place)\n");
		printf("  i    diff two hoggs\n");
		printf("  l    lock a hogg\n");
		printf("  m    delete files based on a file of filespecs\n");
		printf(" flags:\n");
		printf("  2    extra verbose\n");
		printf("  3    just display header information with t, no file list\n");
		printf("  a    preserve pAthname on ./paths\n");
		printf("  i    inverts filespec matching\n");
		printf("  f    out is used as the output name\n");
		printf("  p    pause on exit\n");
		printf("  u    do NOT ignore _underscored folders\n");
		printf("  v    verbose (vv means extra verbose)\n");
		printf("  x    fix files on first verify pass\n");
		printf("  y    automatically overwrite output files\n");
		printf("  s    after syncing, shut down the remote file server\n");
		printf(" other options (must be at END of command line):\n");
		printf("  -Cdir change directory to dir (for extracting)\n");
		printf("  -Rcmd.bat run cmd.bat before executing command (for testing)\n");
		printf("  -Tfilespec.txt instead of a single filespec, load multiple filespecs from a file\n");
		printf("  --nocrash Just exit (with error code) on fatal error\n");
		printf("  --nomutex Does not use multi-app locking/mutex (DANGEROUS)\n");
		printf("  --hogdebug Write out intermediate hogg files for debugging\n");
		printf("  --register Register registry hooks for viewing/opening hoggs\n");
		printf("  --useFileList list.bin Uses a file list instead of scanning\n");
		printf("  --dumpFileList list.bin Dumps a file list for use in --useFileList\n");
		printf("  --symstore \\\\path\\to\\store file.hogg - Symstores contents\n");
		printf("  --respectMirror mirror.txt - respects the mirror filespec and does not prune non-mirrored files\n");
		printf("  --numReaderThreads N - for sync mode, how many threads to read with\n");
		printf("  --journalSize ###  Sets journal size for new hoggs to the specified number of KB\n");
		printf("  --syncMaxSize ###  Sets the max size per hogg file while syncing, in MB (default 2GB)\n");
		printf("  --unsafe  Disables operation journaling (unsafe, for debugging)\n");
		printf("  --superunsafe  Disables operation and datalist journaling (file only valid on exit)\n");
		printf("  --appendOnly  Only appends to output hoggs\n");
		printf("  --ignoreTimestamps  Ignores timestamp differences when doing a diff\n");
		printf("  --tight When defragging, create an optimally tight output file\n");
		printf("  --repair When opening a hogg, skip the no-repair/prompt/repair cycle, simply open as an app would\n");
		printf("  --dontpack Don't compress files in the hogg\n");
		printf(" examples:\n");
		printf("  pig uf path/out.hogg -Tpath/filelist.txt  # Update a hogg file\n");
		printf("  pig u data   # updates data.hogg full of the contents of data/\n");
		printf("  pig xf file.hogg   # Extracts the contents to the current directory\n");
		printf("  pig xf file.hogg *.txt  # Extracts all .txt files to the current directory\n");
		printf("  pig sf prefix data/piggs/*.hogg   # Creates prefix###.hogg full of the contents of the other hoggs\n");
		printf("  pig sf out.hogg in.hogg --syncMaxSize 100000 --superunsafe  # Effectively defrags in.hogg into out.hogg\n");
		printf("  pig f file.hogg # Defrags file.hogg\n");
		printf(" Multiple commands may be separated by a $$, e.g.:\n");
		printf("  pig u data1 $$ u data2\n");
		return 1;
	}

	// Init variables
	verbose=0;
	debug_verbose=0;
	overwrite=0;
	base_folder_set=0;
	shutdown_remote_server=0;
	ignore_underscores=true;
	preserve_pathname=false;
	invert_filespec=false;
	assert(g_mirrorFileSpecName == NULL);
	assert(g_fileListSourceName == NULL);

	switch (argv[1][0]) {
		xcase 'a':
			mode = PIG_ADD;
		xcase 'c':
			mode=PIG_CREATE;
		xcase 'd':
			mode=PIG_DELETE;
		xcase 'g':
			mode=PIG_GENLIST;
		xcase 't':
			mode=PIG_WLIST;
		xcase 'u':
			mode = PIG_UPDATE;
		xcase 'v':
			mode = PIG_VERIFY;
		xcase 'w':
			mode=PIG_LIST;
		xcase 'x':
			mode=PIG_EXTRACT;
		xcase 's':
			mode=PIG_SYNC;
		xcase 'f':
			mode=PIG_DEFRAG;
		xcase 'i':
			mode=PIG_DIFF;
		xcase 'l':
			mode=PIG_LOCK;
		xcase 'm':
			mode=PIG_MASSDELETE;
		xdefault:
			printf("Unknown primary mode: %c\n", argv[1][0]);
	}
	for (i=1; i<(int)strlen(argv[1]); i++)  {
		switch (argv[1][i]) {
			xcase '2':
				debug_verbose=1;
			xcase '3':
				debug_verbose=2;
			xcase 'a':
				preserve_pathname = true;
			xcase 'i':
				invert_filespec = true;
			xcase 'f':
				overrideout=1;
			xcase 'h':
				// default: make_pig_version = HOG_VERSION;
			xcase 'p':
				pause=1;
			xcase 's':
				shutdown_remote_server = true;
			xcase 'u':
				ignore_underscores=false;
			xcase 'v':
				verbose++;
			xcase 'x':
				fix_on_first_verify=true;
			xcase 'y':
				overwrite=1;
			xcase 'z':
				// ignored
				;
			xdefault:
				printf("Unrecognized flag: %c\n", argv[1][i]);
		}
	}

	if (mode != PIG_GENLIST)
		cryptMD5Init();

	for (i=3; i<argc; i++) {
		char *s = argv[i];
#ifndef _XBOX
		if (strStartsWith(s, "-C")) {
			assert(chdir(s+2));
		} else
#endif
		if (strStartsWith(s, "-R")) {
			int r;
			printf("Running external tool %s...\n", s+2);
			r = system(s+2);
			if (r)
				printf("External tool returned %d.\n", r);
			printf("Done running external tool.\n");
		} else if (stricmp(s, "--nocrash")==0) {
			FatalErrorfSetCallback(nocrashCallback, NULL);
		} else if (stricmp(s, "--hogdebug")==0) {
			extern int hog_debug_check;
			hog_debug_check = 1;
		} else if (stricmp(s, "--nomutex")==0) {
			global_flags |= HOG_NO_MUTEX;
		} else if (stricmp(s, "--nodata")==0) {
			extern int hog_mode_no_data;
			hog_mode_no_data = 1;
		} else if (stricmp(s, "--patchtest")==0) {
			return doPatchTest();
		} else if (stricmp(s, "--heavytest")==0) {
			doHeavyTest(0);
			return 0;
		} else if (stricmp(s, "--dcachetest")==0) {
			doHeavyTest(1);
			return 0;
		} else if (stricmp(s, "--dumpFileList")==0) {
			assert(i!=argc-1);
			g_fileListDump = fopen(argv[i+1], "wb");
		} else if (stricmp(s, "--useFileList")==0) {
			assert(i!=argc-1);
			g_fileListSourceName = argv[i+1];
		} else if (stricmp(s, "--respectMirror")==0) {
			assert(i!=argc-1);
			g_mirrorFileSpecName = argv[i+1];
			g_mirrorFileSpec = simpleFileSpecLoad(g_mirrorFileSpecName, "data/");
		} else if (stricmp(s, "--numReaderThreads")==0) {
			assert(i!=argc-1);
			hogSyncSetNumReaderThreads(atoi(argv[i+1]));
		} else if (stricmp(s, "--journalSize")==0) {
			assert(i!=argc-1);
			default_journal_size = atoi(argv[i+1]) * 1024;
		} else if (stricmp(s, "--syncMaxSize")==0) {
			assert(i!=argc-1);
			sync_max_size = atoi(argv[i+1]) * 1024LL * 1024LL;
		} else if (stricmp(s, "--unsafe")==0) {
			hogSetGlobalOpenMode(HogUnsafe);
		} else if (stricmp(s, "--superunsafe")==0) {
			hogSetGlobalOpenMode(HogSuperUnsafe);
		} else if (stricmp(s, "--appendOnly")==0) {
			global_flags |= HOG_APPEND_ONLY;
		} else if (stricmp(s, "--ignoreTimestamps")==0) {
			hog_diff_flags |= HogDiff_IgnoreTimestamps;
		} else if (stricmp(s, "--tight")==0) {
			default_journal_size = 1024;
			defrag_flags |= HogDefrag_Tight;
		} else if (stricmp(s, "--repair")==0) {
			repair_on_first_open = true;
		} else if (stricmp(s, "--dontpack")==0) {
			dont_pack = true;
			defrag_flags |= HogDefrag_DontPack;
		}
	}

	switch (mode) {
		xcase PIG_UPDATE:
			// Intentional fall through
		acase PIG_CREATE:
			// Make out file from argv[2]
			if (overrideout) {
				strcpy(out, argv[2]);
				strcpy(fn, argv[3]);
			} else {
				strcpy(out, argv[2]);
				strcpy(fn, argv[2]);
				while (strEndsWith(out, "/") || strEndsWith(out, "\\")) out[strlen(out)-1]=0;
				strcat(out, ".hogg");
			}
			if (fileExists(out)) {
				if (mode == PIG_CREATE) {
					if (!overwrite) {
						printf("File %s already exists.  Overwrite? [y/N]", out);
						if (!consoleYesNo()) {
							ret = 2;
							break;
						}
					}
					if (fileCanGetExclusiveAccess(out)) {
						// We have access to the file
						fileForceRemove(out);
					} else {
						printf("Failed to open %s, cannot delete, aborting!\n", out);
						ret = 3;
						break;
					}
				}
			} else {
			}
			ret = makePig(fn, out, true, false);
			pak();
		xcase PIG_GENLIST:
			strcpy(fn, argv[2]);
			ret = makePig(fn, NULL, true, false);
			pak();
		xcase PIG_LIST:
			hogSetAllowUpgrade(false); // Don't upgrade a pig we're viewing!
			strcpy(out, argv[2]);
			ret = listPig(out);
			pak();
		xcase PIG_VERIFY:
			hogSetAllowUpgrade(false); // Don't upgrade a pig we're viewing!
			strcpy(out, argv[2]);
			ret = verifyPig(out);
			pak();
		xcase PIG_WLIST:
			hogSetAllowUpgrade(false); // Don't upgrade a pig we're viewing!
			strcpy(out, argv[2]);
#ifndef _XBOX
			ret = win32listPig(out);
#else
			ret = listPig(out);
#endif
		xcase PIG_EXTRACT:
			hogSetAllowUpgrade(false); // Don't upgrade a pig we're viewing!
			strcpy(out, argv[2]);
			if (argc > 3 && argv[3][0]!='-')
				ret = extractPig(out, argv[3]);
			else
				ret = extractPig(out, NULL);
			pak();
		xcase PIG_ADD:
			strcpy(out, argv[2]);
			if (argc <= 3)
				printf("Not enough parameters, expected filename to add.\n");
			else
				ret = makePig(argv[3], out, false, true);
			pak();
		xcase PIG_DELETE:
			strcpy(out, argv[2]);
			if (argc <= 3)
				printf("Not enough parameters, expected filename/filespec to delete.\n");
			else
				ret = deletePig(out, argv[3]);
			pak();
		xcase PIG_MASSDELETE:
			strcpy(out, argv[2]);
			if (argc <= 3)
				printf("Not enough parameters, expected filename of filespecs to delete.\n");
			else
				ret = deleteMassPig(out, argv[3]);
			pak();
		xcase PIG_SYNC:
			if (argc < 4)
			{
				printf("Not enough parameters for hogg syncing\n");
				ret = -1;
			} else {
				ret = syncHogs(argv[2], argv[3]);
			}
			pak();
		xcase PIG_DEFRAG:
			strcpy(out, argv[2]);
			ret = hogDefrag(out, default_journal_size, defrag_flags);
			pak();
		xcase PIG_DIFF:
			if (argc < 4)
			{
				printf("Not enough parameters for hogg diffing\n");
				ret = -1;
			} else {
				ret = hogDiff(argv[2], argv[3], verbose, hog_diff_flags);
			}
			pak();
		xcase PIG_LOCK:
			if (argc < 3)
			{
				printf("Not enough parameters for hogg locking\n");
				ret = -1;
			} else {
				ret = lockHog(argv[2]);
			}
			pak();
		xdefault:
			printf("pig: You must specify one main mode\n");
			ret = -1;
	}
	if (g_fileListDump) {
		fclose(g_fileListDump);
		g_fileListDump = NULL;
	}
	g_fileListSourceName = NULL;

	g_mirrorFileSpecName = NULL;
	if (g_mirrorFileSpec)
	{
		simpleFileSpecDestroy(g_mirrorFileSpec);
		g_mirrorFileSpec = NULL;
	}

	return ret;
}

#if _XBOX

static int memoryTest(void)
{
	void *ptrs[1024];
	size_t sizes[1024];
	int ptrs_count;
	int j;
	U32 flags[] = {
		0,
		PAGE_READWRITE,
		PAGE_READWRITE|PAGE_NOCACHE,
		PAGE_READWRITE|PAGE_WRITECOMBINE,
		PAGE_READWRITE|PAGE_NOCACHE|MEM_LARGE_PAGES,
		PAGE_READWRITE|PAGE_NOCACHE|MEM_16MB_PAGES,
	};
	for (j=0; j<ARRAY_SIZE(flags); j++)
	{
		int i;
		size_t sz = 128*1024*1024;
		printf("Pass %d of %d\n", j+1, ARRAY_SIZE(flags));
		ptrs_count = 0;
		while (sz > 16 && ptrs_count < ARRAY_SIZE(ptrs))
		{
			void *p;
			if (flags[j] == 0)
			{
				p = calloc_canfail(sz, 1);
			} else {
				p = XPhysicalAlloc(sz, MAXULONG_PTR, 0, flags[j]);
			}
			if (p) {
				ptrs[ptrs_count] = p;
				sizes[ptrs_count] = sz;
				ptrs_count++;
			} else {
				sz >>= 1;
			}
		}
		for (i=0; i<ptrs_count; i++)
		{
			int ret;
			printf("Testing %s @ %p\n", friendlyBytes(sizes[i]), ptrs[i]);
			ret = memTestRange(ptrs[i], sizes[i]);
			if (ret)
			{
				printf("Memory test failed\n");
				return ret;
			}
		}
		for (i=0; i<ptrs_count; i++)
		{
			if (flags[j]==0)
				free(ptrs[i]);
			else
				XPhysicalFree(ptrs[i]);
		}
	}
	return 0;
}
#else
static int memoryTest(void)
{
	// Do nothing
	return 0;
}
#endif

int wmain(int argc, WCHAR** argv_wide)
{
	char **argv;
	_CrtMemState g_memstate={0};
	int ret=0;
	char *valid_commands = "-ctxg23adhlpuvyzfw";
	int mode=0;
	int i;
	int overrideout=0;
	int loops=1; // For checking for memory leaks
	int loopcount;
	int timer;

	extern bool g_doHogTiming;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV

	DO_AUTO_RUNS

#ifdef _XBOX
	{
		char *cmdline = strdup(GetCommandLine());
		static char *args[100];
		char *cmdlinedata = fileAlloc("cmdline.txt", NULL);
		if (cmdlinedata) {
			int newsize = strlen(cmdline) + strlen(cmdlinedata) + 2;
			cmdline = realloc(cmdline, newsize);
			strcat_s(cmdline, newsize, " ");
			strcat_s(cmdline, newsize, cmdlinedata);
			free(cmdlinedata);
		}
		argc = tokenize_line(cmdline, args, NULL);
		argv = args;
	}
#endif

	// g_doHogTiming = true;

	disableRtlHeapChecking(NULL);
	memCheckInit();
	fileAllPathsAbsolute(true);
	gimmeDLLDisable(true);
	logDisableLogging(true);
	setDefaultAssertMode();
	{
		extern void tfcInitCriticalSections(void);
		tfcInitCriticalSections(); // Just to pre-init the critical sections.  If this solves the crash, look into it more!
	}
	hogSetGlobalOpenMode(HogSafeAgainstAppCrash);
	stringCacheDoNotWarnOnResize();
#ifdef _XBOX
	hogSetMaxBufferSize(256*1024*1024);
#else
	hogSetMaxBufferSize(512*1024*1024);
#endif
	hogThreadingInit();
	hogSetAllowUpgrade(true);
	dontLogErrors(true);
	sharedMemorySetMode(SMM_DISABLED);
	utilitiesLibStartup();

	pigRegister();

	if (argc>=2 && strcmp("--memtest", argv[1])==0) {
		return memoryTest();
	}

	if (argc==2 && strcmp("--version", argv[1])==0) {
		printf("%s", GetUsefulVersionString());
		return 0;
	}

	if (argc==2 && strcmp("--register", argv[1])==0) {
		pigRegister();
		return 0;
	}

	if (argc==4 && stricmp("--symstore", argv[1])==0) {
		pigSymStore(argv[2], argv[3]);
		return 0;
	}

	if (argc==2 && stricmp("--fileserver", argv[1])==0) {
		fileServerRun();
		return 0;
	}

	timer = timerAlloc();
	timerStart(timer);

	for (loopcount=0; loopcount<loops; loopcount++) {
		//U64 after, before = memMonitorBytesAlloced();
		//_CrtMemCheckpoint(&g_memstate);
		int indexToRun=0;
		char *temp;
		int r2;
		for (i=0; i<argc; i++) {
			if (stricmp(argv[i], "$$")==0) {
				temp = argv[indexToRun];
				argv[indexToRun] = argv[0];
				r2 = pigMainRun(i - indexToRun, argv + indexToRun);
				argv[indexToRun] = temp;
				indexToRun = i;
				if (r2)
					ret = r2;
			}
		}
		temp = argv[indexToRun];
		argv[indexToRun] = argv[0];
		r2 = pigMainRun(argc - indexToRun, argv + indexToRun);
		argv[indexToRun] = temp;
		if (r2)
			ret = r2;

		//_CrtMemDumpAllObjectsSince(&g_memstate);
		//after = memMonitorBytesAlloced();
		//printf("Memory before: %"FORM_LL"d after: %"FORM_LL"d\n", before, after);
		//memMonitorDisplayStats();
	}
	//printf("main(): %fs\n", timerElapsed(timer));
	//{int a = _getch(); }
	timerFree(timer);

	EXCEPTION_HANDLER_END

	return ret;
}

#include "AutoGen/pig_c_ast.c"
