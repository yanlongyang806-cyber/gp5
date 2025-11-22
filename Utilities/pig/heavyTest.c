#include "heavyTest.h"
#include "hoglib.h"
#include "piglib.h"
#include "timing.h"
#include "utils.h"
#include "file.h"
#include "StringCache.h"
#include "mathutil.h"
#include "wininclude.h"

AUTO_RUN_FIRST;
void initStringCache(void)
{
#if PLATFORM_CONSOLE
	stringCacheSetInitialSize(1*1024*1024);
#else
	stringCacheSetInitialSize(8*1024*1024);
#endif
	stringCacheSetGrowFast();
}

void doHeavyTest(int read)
{
#ifndef _XBOX
	// Run two of these in parallel to test this
	char filenames[][MAX_PATH] = {
		"1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111.txt",
		"222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222.txt",
		"33333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333.txt",
	};
	char *data[10];
	int data_size[ARRAY_SIZE(data)];
	int i;
	int timestamp =1;
	int timer=timerAlloc();
	int fgTimer=timerAlloc();
	int bgTimer=timerAlloc();
	int totalTimer=timerAlloc();
	F32 fgElapsed;
	F32 bgElapsed;
	int count=0;
	int totalCount=0;
	HogFile *hog_file;
	int j;
	enum {
		ModeRepeatUpdate,
		ModeIncrementalAdd,
		ModeUpdateTimestampsAndRead,
	} mode = ModeRepeatUpdate;
	F32 maxSize = 1024*1024;

	if (read)
		mode = ModeUpdateTimestampsAndRead;

	if (mode == ModeRepeatUpdate) {
		maxSize = 1024;
	} else {
		maxSize = 1024;
	}
//#define DO_HEADERS
#ifdef DO_HEADERS
	for (i=0; i<ARRAY_SIZE(filenames); i++) {
		changeFileExt(filenames[i], ".wtex", filenames[i]);
	}
#endif

	hogSetGlobalOpenMode(HogSafeAgainstAppCrash);
	hogSetMaxBufferSize(128*1024*1024);

	for (j=0; j<1; j++) {

	hog_file = hogFileRead("./testing.hogg", NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
	assert(hog_file);
	//hogFileSetSingleAppMode(hog_file, true);
	for (i=0; i<ARRAY_SIZE(data); i++) {
#ifdef DO_HEADERS
		char fn[MAX_PATH];
		int newsize;
		sprintf(fn, "./headertest%d.wtex", i);
		data[i] = fileAlloc(fn, &data_size[i]);
		assert(data[i]);
		newsize = rand()*maxSize/(F32)(RAND_MAX+1);
		MIN1(data_size[i], newsize);
#else
		data_size[i] = rand()*maxSize/(F32)(RAND_MAX+1);
		data[i] = malloc(data_size[i]);
		memset(data[i], 0x77, data_size[i]);
#endif
	}
	while (!_kbhit()) {
		char buf[10];
		F32 elapsed;
		int index = rand()*ARRAY_SIZE(data)/(RAND_MAX+1);
		char *rand_filename = filenames[rand()*ARRAY_SIZE(filenames)/(RAND_MAX+1)];
		int file_index;
		file_index = hogFileFind(hog_file, rand_filename);
		if (mode == ModeRepeatUpdate || (mode == ModeUpdateTimestampsAndRead && HOG_INVALID_INDEX == file_index)) {
#define DO_ASYNC
#ifdef DO_ASYNC
			hogFileModifyUpdateNamedAsync
#else
			hogFileModifyUpdateNamedSync
#endif
				(hog_file, rand_filename, memdup(data[index], data_size[index]), data_size[index], ++timestamp, NULL);
// #define READ_BACK_IMMEDIATELY
#ifdef READ_BACK_IMMEDIATELY
			hogFileExtractBytes(hog_file, file_index, buf, 0, ARRAY_SIZE(buf));
#endif
		} else if (mode == ModeIncrementalAdd) {
			char name[MAX_PATH];
			sprintf(name, "file_%d", totalCount);
#ifdef DO_ASYNC
			hogFileModifyUpdateNamedAsync
#else
			hogFileModifyUpdateNamedSync
#endif
				(hog_file, name, memdup(data[index], data_size[index]), data_size[index], ++timestamp, NULL);
			file_index = hogFileFind(hog_file, name);
#ifdef READ_BACK_IMMEDIATELY
			hogFileExtractBytes(hog_file, file_index, buf, 0, ARRAY_SIZE(buf));
#endif
		} else if (mode == ModeUpdateTimestampsAndRead) {
			hogFileModifyUpdateTimestamp(hog_file, file_index, ++timestamp);
			//Sleep(1);
			hogFileExtractBytes(hog_file, file_index, buf, 0, ARRAY_SIZE(buf));
		}
		//if ((rand()%7)==1) // To allow multi-process to have a chance
		//	hogFileModifyFlush(hog_file);
		count++;
		totalCount++;
		if ((elapsed = timerElapsed(timer)) > 1) {
			extern int g_fflush_count;
			int v = g_fflush_count;
			g_fflush_count = 0;
			timerStart(timer);
			printf("%1.1f queued updates/s (%"FORM_LL"d queued bytes)  %d fflushes\n", count / elapsed, hogFileGetQueuedModSize(hog_file), v);
			count = 0;
		}
	};
	fgElapsed = timerElapsed(fgTimer);
	printf("Closing...");
	hogFileDestroy(hog_file, true);
	printf(" done.\n");
	bgElapsed = timerElapsed(bgTimer);
	printf("FG: %1.1f u/s  BG: %1.1f u/s\n", totalCount / fgElapsed, totalCount / bgElapsed);
	}
	printf("Total time: %1.1fs\n", timerElapsed(totalTimer));
	{
		int k = _getch();
	}
#endif
}
