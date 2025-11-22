#include "file.h"
#include "fileutil.h"
#include "piglib.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include "mathutil.h"
#include <ctype.h>
#include <string.h>
#include "rand.h"
#include "sysutil.h"
#include "timing.h"
#include "zutils.h"
#include "StashTable.h"
#include "UnitSpec.h"
#include "GlobalTypes.h"
#include <conio.h>
#include "FolderCache.h"
#include "utils.h"
#include <process.h>
#include "hoglib.h"
#include "crypt.h"
#include "error.h"
#include "earray.h"
#include "fileCache.h"
#include "cmdparse.h"
#include "memlog.h"
#include "SharedMemory.h"

extern int folderMonTest(void);
extern int folderMonTest2(void);

extern MemLog hogmemlog;
extern int hog_verbose;

static int promptRanged(char *str, int max) { // Assumes min of 1, max can't be more than 9
	int ret=-1;
	do {
		printf("%s: [1-%d] ", str, max);
		ret = getch() - '1';
		printf("\n");
	} while (ret<0 || ret>=max);
	return ret;
}

char *files[] = {
// 	"/texture_library/ENEMIES/Tsoo/Chest_Skin_Tsoo_Vest.texture",
// 	"/texture_library/WORLD/nature/foliage/bushes/cat_tails.texture", 
// 	"/player_library/zombie.seq", 
// 	"/player_library/fem_head.anm",
	"bin/geobin/ol/S/B/BuildingGen.mset", // not compressed
	"texture_library/system/engine/random.wtex",
	"bin/kb.bin",
	"bin/DynMoves.bin", // big
};

char *files_data[ARRAY_SIZE(files)];
int files_size[ARRAY_SIZE(files)];

void readerfunc(void *junk) {
	intptr_t threadnum = (intptr_t)junk;
	int j=0;
	FILE *f;
	int len;
	char *data;
	//while (true) {
	while(true) {
		int i = rand()*ARRAY_SIZE(files)/RAND_MAX;
		int openstyle = rand()*2/RAND_MAX;
		//printf("%d: (%d) %s\n", threadnum, openstyle, files[i]);
		switch(openstyle) {
		case 0:
			f = fileOpen(files[i], "r");
			fileClose(f);
			break;
		case 1:
			data = fileAlloc(files[i], &len);
			fileFree(data);
			break;
		}
	}
}

extern int is_pigged_path(const char *path);

// Test to find bug when two threads are loading files at the same time
// this never actually found anything, because the bug only crops
// up when we're waiting on the filesystem at an inopportune time
void testThreadedPigLoading() {
	int count[2]={0,0};
	int i;
	char fn[MAX_PATH];
	// init
	for (i=0; i<ARRAY_SIZE(files); i++) {
		fileLocateRead(files[i], fn);
		printf("%s:", files[i]);
		if (is_pigged_path(fn)) {
			printf("\tPIGGED\n");
			count[0]++;
		} else {
			printf("\tNOT pigged\n");
			count[1]++;
		}
	}
	if (count[0]<2) {
		printf("Error: expected at least 2 pigged files in the list.  Repigg some up\n");
		gets(fn);
		return;
	} else if (count[1]<2) {
		printf("Error: expected at least 2 files not pigged in the list.  Touch some\n");
		gets(fn);
		return;
	}
	_beginthread(readerfunc, 0, (void*)1);
	readerfunc(0);

}

void testFileDeleted(void *junk, const char* path)
{
	printf("File deleted: %s\n", path);
}

void testFileNewUpdated(void *junk, const char* path, U32 filesize, U32 timestamp, HogFileIndex file_index)
{
	printf("File new updated: %s  size:%d  timestamp:%d\n", path, filesize, timestamp);
}

void multiProcessHogLockTest(void)
{
	while (true) {
		int i = randomIntRange(0, ARRAY_SIZE(files)-1);
		if (fileExists(files[i])) {
			if (strStartsWith(files[i], "bin")) {
				fileUpdateHoggAfterWrite(files[i], NULL, 0);
			} else {
				fileFree(fileAlloc(files[i], NULL));
			}
			printf(".");
		}
	}
}

void deleteThenCreateTest(void)
{
	HogFile *hog_file;
	HogFileIndex index;
	int count=0;
	hog_file = hogFileRead("./pigtest.hogg", NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
	assert(hog_file);
	while (!kbhit())
	{
		int i = 0; // randomIntRange(0, ARRAY_SIZE(files)-1);
		if (!files_data[i])
		{
			files_data[i] = fileAlloc(files[i], &files_size[i]);
		}

		index = hogFileFind(hog_file, files[i]);
		if (index != HOG_INVALID_INDEX) {
			memlog_printf(&hogmemlog, "deleteThenCreateTest: deleting");
			hogFileModifyUpdateNamedSync(hog_file, files[i], NULL, 0, 0, NULL);
			memlog_printf(&hogmemlog, "deleteThenCreateTest: flushing");
			hogFileModifyFlush(hog_file);
			memlog_printf(&hogmemlog, "deleteThenCreateTest: updating");
			hogFileModifyUpdateNamedSync(hog_file, files[i], memdup(files_data[i], files_size[i]), files_size[i], time(NULL), NULL);
		} else {
			memlog_printf(&hogmemlog, "deleteThenCreateTest: adding new");
			hogFileModifyUpdateNamedSync(hog_file, files[i], memdup(files_data[i], files_size[i]), files_size[i], time(NULL), NULL);
		}
		memlog_printf(&hogmemlog, "deleteThenCreateTest: flushing");
		hogFileModifyFlush(hog_file);
		if (count++%10==0)
			printf(".");
	}
	hogFileDestroy(hog_file, true);
}

void multiProcessHogModTest(void)
{
	HogFile *hog_file;
	int ret;
	int i=0;
	hog_file = hogFileRead("./pigtest.hogg", NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
	assert(hog_file);
	hogFileSetCallbacks(hog_file, NULL, testFileNewUpdated);
	while (true) {
		int choice=0;
		printf("Operation:\n");
		printf("  1. Add\n");
		printf("  2. Update\n");
		printf("  3. Delete\n");
		printf("  4. Query\n");
		printf("  5. Read\n");
		printf("  6. Flush\n");
		printf("  7. Set Skip Mutex\n");
		printf("  8. Unset Skip Mutex\n");
		choice = promptRanged("Do what", 8);
		if (choice==0) {
			if (hogFileFind(hog_file, "test.txt")==HOG_INVALID_INDEX) {
				NewPigEntry entry={0};
				entry.data = malloc(16);
				quick_sprintf(entry.data, 16, "Mod:%d", ++i);
				printf("Adding test.txt with data: \"%s\"\n", entry.data);
				entry.size = 16;
				entry.fname = "test.txt";
				entry.timestamp = i;
				ret = hogFileModifyUpdateNamed2(hog_file, &entry);
				assert(ret==0 || ret==-1);
			} else {
				printf("Already there!\n");
			}
		}
		if (choice==1) {
			HogFileIndex index;
			if ((index = hogFileFind(hog_file, "test.txt"))==HOG_INVALID_INDEX) {
				printf("Not found!\n");
			} else {
				NewPigEntry entry={0};
				entry.data = malloc(16);
				quick_sprintf(entry.data, 16, "Mod:%d", ++i);
				printf("Updating test.txt with data: \"%s\"\n", entry.data);
				entry.size = 16;
				entry.fname = "test.txt";
				entry.timestamp = i;
				ret = hogFileModifyUpdateNamed2(hog_file, &entry);
				assert(ret==0 || ret==-1);
			}
		}
		if (choice==2) {
			ret = hogFileModifyDeleteNamed(hog_file, "test.txt");
			assert(ret==0 || ret==-1);
		}
		if (choice==3) {
			HogFileIndex index = hogFileFind(hog_file, "test.txt");
			if (index == HOG_INVALID_INDEX) {
				printf("Not found.\n");
			} else {
				printf("Found, size : %d timestamp : %d\n", hogFileGetFileSize(hog_file, index), hogFileGetFileTimestamp(hog_file, index));
			}
		}
		if (choice==4) {
			HogFileIndex index = hogFileFind(hog_file, "test.txt");
			if (index == HOG_INVALID_INDEX) {
				printf("Not found.\n");
			} else {
				U32 count=0;
				U8 *data = hogFileExtract(hog_file, index, &count, NULL);
				printf("Data: \"%s\" size: %d\n", data, count);
			}
		}
		if (choice==5) {
			ret = hogFileModifyFlush(hog_file);
			assert(ret==0);
		}
		if (choice==6) {
			hogFileSetSkipMutex(hog_file, true);
		}
		if (choice==7) {
			hogFileSetSkipMutex(hog_file, false);
		}
	}
}

static volatile int dummyThreadDone=0;
int __stdcall dummyThread(void *param)
{
	HogFile *hog_file = param;
	HogFileIndex index;
	index = hogFileFind(hog_file, "testfile.txt");
	if (index == HOG_INVALID_INDEX) {
		hogFileModifyUpdateNamed(hog_file, "testfile.txt", strdup("blarg"), (U32)strlen("blarg"), time(NULL), NULL);
	} else {
		if (randomIntRange(0, 5)!=0) {
			hogFileModifyUpdateNamed(hog_file, "testfile.txt", NULL, 0, 0, NULL);
		} else {
			if (randomIntRange(0,4)!=0)
			{
				hogFileModifyUpdateNamed(hog_file, "testfile.txt", strdup("blarg2"), (U32)strlen("blarg2"), time(NULL), NULL);
			} else {
				int size;
				void *data = hogFileExtract(hog_file, index, &size, NULL);
				free(data);
			}
		}
	}
	dummyThreadDone = 1;
	return 0;
}

int __stdcall dummyThread2(void *param)
{
	HogFile *hog_file = param;
	HogFileIndex index;
	index = hogFileFind(hog_file, "testfile.txt");
	if (index == HOG_INVALID_INDEX) {
		hogFileModifyUpdateNamed(hog_file, "testfile.txt", strdup("blarg"), (U32)strlen("blarg"), time(NULL), NULL);
	} else {
		int size;
		void *data = hogFileExtract(hog_file, index, &size, NULL);
		free(data);
	}
	index = hogFileFind(hog_file, "testfile2.txt");
	if (index == HOG_INVALID_INDEX) {
		hogFileModifyUpdateNamed(hog_file, "testfile2.txt", strdup("blarg2"), (U32)strlen("blarg2"), time(NULL), NULL);
	} else {
		int size;
		void *data = hogFileExtract(hog_file, index, &size, NULL);
		free(data);
	}
	dummyThreadDone = 1;
	return 0;
}

void testOpenClose(void)
{
	HogFile *hog_file;
	int i=0;
	int timer = timerAlloc();
	fileAllPathsAbsolute(true);
	fileDisableAutoDataDir();
	while (!kbhit()) {
		char hogname[1024];
		sprintf(hogname, "./pigtest_%d.hogg", i%100);
		//loadstart_printf("Loading hogg %s...", hogname);
		hog_file = hogFileRead(hogname, NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
		//hog_file = hogFileRead("C:/FightClub/data/piggs/bin.hogg", NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
		
		//loadend_printf("done.");
		assert(hog_file);
		dummyThreadDone = 0;
		if (1)
		{
			dummyThread2(hog_file);
		} else if (0)
		{
			CloseHandle((HANDLE)_beginthreadex(NULL, 0, dummyThread, hog_file, 0, NULL));
		} else {
			dummyThread(hog_file);
		}
		while (!dummyThreadDone)
			Sleep(1);
		hogFileDestroy(hog_file, true);
		//calloc(328,1);
		if (i++%10==0)
			printf(".");
	}
	printf("\n%d in %fs (%fms/open)\n", i, timerElapsed(timer), timerElapsed(timer) / i * 1000);
	timerFree(timer);
	exit(1);
}

char *bubble_data;
int bubble_data_size;
int __stdcall dummyThread3(void *param)
{
	HogFile *hog_file = param;
	hogFileModifyUpdateNamedSync(hog_file, "testfile.txt", memdup(bubble_data, bubble_data_size), bubble_data_size, time(NULL), NULL);
	dummyThreadDone = 1;
	return 0;
}

void testOpenClose2(void)
{
	HogFile *hog_file;
	int i=0;
	int timer = timerAlloc();
	fileAllPathsAbsolute(true);
	fileDisableAutoDataDir();
	bubble_data_size = 1000000;
	bubble_data = malloc(bubble_data_size);
	for (i=0; i<bubble_data_size; i++)
		bubble_data[i] = randomIntRange(0, 255);
	while (!kbhit()) {
		HogFile *hog_file_bubble;
		char hogname[1024];
		void *ignoreme;

		// Queue bubble
		sprintf(hogname, "./pigtest_bubble_%d.hogg", i%100);
		hog_file_bubble = hogFileRead(hogname, NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
		dummyThreadDone = 0;
		CloseHandle((HANDLE)_beginthreadex(NULL, 0, dummyThread3, hog_file_bubble, 0, NULL));

		sprintf(hogname, "./pigtest_%d.hogg", i%100);
		hog_file = hogFileRead(hogname, NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
		hogFileDestroy(hog_file, true);
		ignoreme = malloc(328);

 		//while (!dummyThreadDone)
 		//	Sleep(1);

		//hogFileDestroy(hog_file_bubble, true);

		if (i++%10==0)
			printf(".");
	}
	printf("\n%d in %fs (%fms/open)\n", i, timerElapsed(timer), timerElapsed(timer) / i * 1000);
	timerFree(timer);
	exit(1);
}

void createThenDeleteTest(void)
{
	HogFile *hog_file;
	HogFileIndex index;
	int i=0;
	hog_file = hogFileRead("./pigtest.hogg", NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
	assert(hog_file);
	while (!kbhit())
	{
		char name[1024];
		sprintf(name, "%d.txt", i);
		index = hogFileFind(hog_file, name);
		if (index == HOG_INVALID_INDEX) {
			hogFileModifyUpdateNamed(hog_file, name, strdup("blarg"), (U32)strlen("blarg"), time(NULL), NULL);
			hogFileModifyUpdateNamed(hog_file, name, NULL, 0, 0, NULL);
		} else {
			hogFileModifyUpdateNamed(hog_file, name, NULL, 0, 0, NULL);
		}
		if (i++%10==0)
			printf(".");
	}
	hogFileDestroy(hog_file, true);

}

int __stdcall multipleUpdatesTestThread(void *param)
{
	HogFile *hog_file = param;
	while (!_kbhit())
	{
		hogFileModifyUpdateNamedAsync(hog_file, "testfile.txt", strdup("blarg"), (U32)strlen("blarg"), time(NULL), NULL);
	}
	InterlockedIncrement(&dummyThreadDone);
	return 0;
}

volatile int file_update_name=-1;
volatile int master_wait=0;
int __stdcall multipleUpdatesTestThreadMaster(void *param)
{
	HogFile *hog_file = param;
	while (!_kbhit())
	{
		char buf[1024];
		char *data = strdup("blarg");
		master_wait = 1;
		InterlockedIncrement(&file_update_name);

		sprintf(buf, "testfile_%d", file_update_name);
		verify(0==hogFileModifyUpdateNamedAsync(hog_file, buf, data, 5, 6, NULL));
		while (master_wait)
			;
	}
	file_update_name = -2;
	InterlockedIncrement(&dummyThreadDone);
	return 0;
}
int __stdcall multipleUpdatesTestThreadSlave(void *param)
{
	HogFile *hog_file = param;
	int last_file_update_name=-1;
	while (file_update_name!=-2)
	{
		char buf[1024];
		char *data = strdup("blarg");
		master_wait = 0;
		while (file_update_name == last_file_update_name)
			;
		sprintf(buf, "testfile_%d", file_update_name);
		verify(0==hogFileModifyUpdateNamedAsync(hog_file, buf, data, 5, 1, NULL));
		last_file_update_name = file_update_name;
	}
	InterlockedIncrement(&dummyThreadDone);
	return 0;
}

void multipleUpdatesTest(void)
{
	HogFile *hog_file;
	int i=0;
	int timer = timerAlloc();
	fileAllPathsAbsolute(true);
	fileDisableAutoDataDir();
	while (!kbhit()) {
		int num_threads;
		char hogname[1024];
		sprintf(hogname, "./pigtest_%d.hogg", i%100);
		fileForceRemove(hogname);
		//loadstart_printf("Loading hogg %s...", hogname);
		hog_file = hogFileRead(hogname, NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
		//hog_file = hogFileRead("C:/FightClub/data/piggs/bin.hogg", NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);

		//loadend_printf("done.");
		assert(hog_file);
		dummyThreadDone = 0;
		if (0)
		{
			multipleUpdatesTestThread(hog_file);
			num_threads = 1;
		} else {
			num_threads = 2;
			CloseHandle((HANDLE)_beginthreadex(NULL, 0, multipleUpdatesTestThreadMaster, hog_file, 0, NULL));
			for (i=1; i<num_threads; i++)
			{
				CloseHandle((HANDLE)_beginthreadex(NULL, 0, multipleUpdatesTestThreadSlave, hog_file, 0, NULL));
			}
		}
		while (dummyThreadDone!=num_threads)
			Sleep(1);
		hogFileDestroy(hog_file, true);
		//calloc(328,1);
		if (i++%10==0)
			printf(".");
	}
	printf("\n%d in %fs (%fms/open)\n", i, timerElapsed(timer), timerElapsed(timer) / i * 1000);
	timerFree(timer);
	exit(1);
}


void stressTestHoggWriting(void)
{
	const char* hogFileName = "stressTest.hogg";
	const S32	fileCount = 50000;
	HogFile*	hf;
	
	unlink(hogFileName);
	
	assert(!fileExists(hogFileName));
	
	// Create the hogg.
	
	printf("Creating test hogg...");
	
	hf = hogFileRead(hogFileName, NULL, PIGERR_PRINTF, NULL, HOG_DEFAULT);
	
	assert(hf);
	
	FOR_BEGIN(i, fileCount);
		char	fileName[100];
		char*	fileData;
		
		sprintf(fileName, "FileName%5.5d", i);
		fileData = strdup(fileName);
		hogFileModifyUpdateNamedAsync(hf, fileName, fileData, (U32)strlen(fileData) + 1, time(NULL), NULL);
	FOR_END;
	
	hogFileDestroy(hf, true);
	hf = NULL;
	
	printf("Done!\n");

	// Read it.
	
	printf("Reading test hogg...\n");
	
	hf = hogFileRead(hogFileName, NULL, PIGERR_PRINTF, NULL, HOG_DEFAULT);
	
	assert(hf);
	
	FOR_BEGIN(i, fileCount);
		char			fileName[100];
		char*			fileData;
		U32				fileDataBytes;
		HogFileIndex	hfi;
		
		sprintf(fileName, "FileName%5.5d", i);
		hfi = hogFileFind(hf, fileName);
		if(hfi == HOG_INVALID_INDEX){
			printf(	"  File \"%s\" is not in the hogg.\n",
					fileName);
			continue;
		}
		
		fileData = hogFileExtract(hf, hfi, &fileDataBytes, NULL);
		
		assert(fileData);
		
		fileData[fileDataBytes - 1] = 0;
		if(stricmp(fileName, fileData)){
			printf(	"  File \"%s\" contains \"%s\".\n",
					fileName,
					fileData);
		}
	FOR_END;
	
	hogFileDestroy(hf, true);
	hf = NULL;

	printf("Done reading test hogg!\n");
}

void simpleObjectDBtest(void)
{
	HogFile *hog_file;
	int timer = timerAlloc();
	int maxdatasize = 250500;
	char *base_data = malloc(maxdatasize);
	int r = 0;
	while (r<maxdatasize)
	{
		int v = randInt(100);
		int j;
		unsigned int v2 = randInt(256);
		MIN1(v, maxdatasize - r);
		for (j=0; j<v; j++, r++, v2++)
		{
			base_data[r] = v2 & 0xff;
		}
	}

	fileAllPathsAbsolute(true);
	fileDisableAutoDataDir();

	while (!kbhit())
	{
		int i=0;
		int mx = randInt(15000);
		fileForceRemove("pigtest.hogg");
		hog_file = hogFileRead("pigtest.hogg", NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
		hogFileSetSingleAppMode(hog_file, true);
		assert(hog_file);
		while (i < mx) {
			int index = randInt(1000);
			char name[MAX_PATH];
			unsigned char *data;
			int size;
			sprintf(name, "%d.con", index);
			size = 500+randInt(250000);
			data = malloc(size);
			memcpy(data, base_data, size);

			hogFileModifyUpdateNamed(hog_file, name, data, size, _time32(NULL), NULL);

			if (i++%100==0)
				printf(".");
		}
		hogFileDestroy(hog_file, true);

		// Verify
		hog_file = hogFileRead("pigtest.hogg", NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
		assert(hog_file);
		{
			char *sret=NULL;
			bool bret = hogFileVerifyToEstr(hog_file, &sret, false);
			assert(bret);
			estrDestroy(&sret);
		}
		hogFileDestroy(hog_file, true);
		printf("\n#");
	}
	timerFree(timer);
	exit(1);
}

void testDestroy(void)
{

}


void testThreadedQueuedDeletes(void)
{
	HogFile *hog_file;
	int timer = timerAlloc();
	int maxdatasize = 250500;
	char *base_data = malloc(maxdatasize);
	int r = 0;
	while (r<maxdatasize)
	{
		int v = randInt(100);
		int j;
		unsigned int v2 = randInt(256);
		MIN1(v, maxdatasize - r);
		for (j=0; j<v; j++, r++, v2++)
		{
			base_data[r] = v2 & 0xff;
		}
	}

	fileAllPathsAbsolute(true);
	fileDisableAutoDataDir();

	while (!kbhit())
	{
		char name[MAX_PATH];
		int i=0;
		int mx = randInt(1000);
		fileForceRemove("pigtest.hogg");
		hog_file = hogFileRead("pigtest.hogg", NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
		hogFileSetSingleAppMode(hog_file, true);
		assert(hog_file);
		while (i < mx) {
			int index = randInt(1000);
			unsigned char *data;
			int size;
			sprintf(name, "%d.con", index);
			size = 500+randInt(2500);
			data = malloc(size);
			memcpy(data, base_data, size);

			hogFileModifyUpdateNamed(hog_file, name, data, size, _time32(NULL), NULL);

			if (i++%100==0)
				printf(".");
		}
		hogFileModifyFlush(hog_file);

		// Fill up to max files
		i = 0;
		while (hogFileGetNumUsedFiles(hog_file) < hogFileGetNumFiles(hog_file)-1 || i < 100)
		{
			unsigned char *data;
			int size;
			sprintf(name, "%d.fillcon", i);
			size = 500+randInt(2500);
			data = malloc(size);
			memcpy(data, base_data, size);

			hogFileModifyUpdateNamed(hog_file, name, data, size, _time32(NULL), NULL);

			if (i++%100==0)
				printf(".");
		}
		hogFileModifyFlush(hog_file);

		// Now, delete some and immediately add a bunch more
		for (i=0; i<100; i++)
		{
			sprintf(name, "%d.fillcon", i);
			hogFileModifyDeleteNamed(hog_file, name);
		}
		for (i=0; i<200; i++)
		{
			unsigned char *data;
			int size;
			sprintf(name, "%d.newcon", i);
			size = 10;
			data = malloc(size);
			memcpy(data, base_data, size);

			hogFileModifyUpdateNamed(hog_file, name, data, size, _time32(NULL), NULL);
		}

		hogFileDestroy(hog_file, true);
		printf("\n#");
	}
	timerFree(timer);
	exit(1);
}

void testUpdateThenDelete(void)
{
	HogFile *hog_file;
	int timer = timerAlloc();
	int maxdatasize = 250500;
	char *base_data = malloc(maxdatasize);
	int r = 0;
	while (r<maxdatasize)
	{
		int v = randInt(100);
		int j;
		unsigned int v2 = randInt(256);
		MIN1(v, maxdatasize - r);
		for (j=0; j<v; j++, r++, v2++)
		{
			base_data[r] = v2 & 0xff;
		}
	}

	fileAllPathsAbsolute(true);
	fileDisableAutoDataDir();

	while (!kbhit())
	{
		char name[MAX_PATH];
		int i=0;
		int mx = randInt(1000);
		fileForceRemove("pigtest.hogg");
		hog_file = hogFileRead("pigtest.hogg", NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
		hogFileSetSingleAppMode(hog_file, true);
		assert(hog_file);
		// create
		for (i=0; i<mx; i++)
		{
			unsigned char *data;
			int size;
			sprintf(name, "%d.con", i);
			size = 5+randInt(2500);
			data = malloc(size);
			memcpy(data, base_data, size);

			hogFileModifyUpdateNamed(hog_file, name, data, size, _time32(NULL), NULL);

			if (i%100==0)
				printf(".");
		}
		hogFileModifyFlush(hog_file);

		// Update
		for (i=0; i<mx; i++)
		{
			unsigned char *data;
			int size;
			sprintf(name, "%d.con", i);
			size = 5+randInt(2500);
			data = malloc(size);
			memcpy(data, base_data, size);

			hogFileModifyUpdateNamed(hog_file, name, data, size, _time32(NULL), NULL);

			if (i%100==0)
				printf(".");
		}

		// Now, delete the recently added files
		for (i=0; i<mx; i++)
		{
			sprintf(name, "%d.con", i);
			hogFileModifyDeleteNamed(hog_file, name);
		}

		// re-update for good measure
		for (i=0; i<mx; i++)
		{
			unsigned char *data;
			int size;
			sprintf(name, "%d.con", i);
			size = 5+randInt(2500);
			data = malloc(size);
			memcpy(data, base_data, size);

			hogFileModifyUpdateNamed(hog_file, name, data, size, _time32(NULL), NULL);

			if (i%100==0)
				printf(".");
		}

		hogFileDestroy(hog_file, true);
		printf("\n#");
	}
	timerFree(timer);
	exit(1);
}

void extractDataFromCorruptPigStuff(void)
{
	U32 hash[4];
	int len, zipped_good_len;
	char *data, *zipped_good_data;
	U8 *unzipped_data;
	U8 *final_data;
	int final_len=0;
	char *s;
	char *args[10];
	int argc;
	HogFile *h;
	HogFileIndex file;
	bool checksum_valid;
	int unzipped_len;
	FILE *test;
	const char *filename = "bin/geobin/ol/Z/A/R/Mil_Ren_Plaz.mset";
	const char *hogname = "E:/Games/NVIDIA/FightClubClient/piggs/object.hogg";

	fileAllPathsAbsolute(true);
	h = hogFileRead(hogname, NULL, PIGERR_ASSERT, NULL, HOG_NOCREATE|HOG_READONLY);
	assert(h);
	file = hogFileFind(h, filename);
	assert(file != HOG_INVALID_INDEX);
	unzipped_data = hogFileExtract(h, file, &unzipped_len, &checksum_valid);
	assert(checksum_valid);
	printf("unzipped len: %d\n", unzipped_len);

	zipped_good_data = hogFileExtractCompressed(h, file, &zipped_good_len);
	printf("zipped len: %d\n", zipped_good_len);

	s = data = fileAlloc("c:/temp.txt", &len);
	final_data = malloc(len/2);
	if (1)
	{
		// watch window (U8*)var,3000 output
		while (argc = tokenize_line(s, args, &s))
		{
			assert(argc>=2);
			final_data[final_len++] = (U8)(U32)atoi(args[1]);
		}
	} else {
		// WinDbg "db addr LsizeInHex" output
		while ( s )
		{
			int i;
			int slen = (data + len) - s;
			while (*s=='\n' || *s=='\r')
				s++;
			for (i=0; i<16; i++)
			{
				int v=0;
				int offs = 10 + i*3;
				if (offs + 3 >= slen)
					break;
				if (s[offs]==' ')
					break;
				if (s[offs] >= '0' && s[offs]<='9')
					v += s[offs] - '0';
				else if (s[offs]>='a' && s[offs]<='f')
					v += s[offs] - 'a' + 0xa;
				else
					assert(0);
				v <<= 4;
				if (s[offs+1] >= '0' && s[offs+1]<='9')
					v += s[offs+1] - '0';
				else if (s[offs+1]>='a' && s[offs+1]<='f')
					v += s[offs+1] - 'a' + 0xa;
				else
					assert(0);
				final_data[final_len++] = (U8)v;
			}
			s = strchr(s, '\n');
		}
	}
	assert(final_len == zipped_good_len || final_len == unzipped_len);
	fileFree(data);

	if (final_len == unzipped_len)
	{
		printf("cmp %d\n", memcmp(unzipped_data, final_data, final_len));
		cryptMD5(final_data, final_len, hash);
		printf("bad CRC: %08X\n", hash[0]);
		test = fopen("C:/test.bad.unzipped", "wb");
		fwrite(final_data, 1, final_len, test);
		fclose(test);
	} else {
		printf("cmp %d\n", memcmp(zipped_good_data, final_data, final_len));
		test = fopen("C:/test.bad.zipped", "wb");
		fwrite(final_data, 1, final_len, test);
		fclose(test);
	}

	{
		test = fopen("C:/test.good.unzipped", "wb");
		cryptMD5(unzipped_data, unzipped_len, hash);
		printf("good CRC: %08X	%08X\n", hash[0], hogFileGetFileChecksum(h, file));
		fwrite(unzipped_data, 1, unzipped_len, test);
		fclose(test);
	}
	{
		test = fopen("C:/test.good.zipped", "wb");
		fwrite(zipped_good_data, 1, zipped_good_len, test);
		fclose(test);
	}

	{
		U32 outsize = unzipped_len;
		char *buffer = malloc(unzipped_len);
		int ret = unzipDataEx(buffer, &outsize, final_data, final_len, true);
		printf("unzipData Ret: %d\n", ret);
		free(buffer);
	}

	printf("");
	free(final_data);
	free(unzipped_data);
	free(zipped_good_data);
}

void testCreateThenDelete(void)
{
	HogFile *hog_file;
	int timer = timerAlloc();
	int maxdatasize = 250500;
	char *base_data = malloc(maxdatasize);
	int r = 0;
	while (r<maxdatasize)
	{
		int v = randInt(100);
		int j;
		unsigned int v2 = randInt(256);
		MIN1(v, maxdatasize - r);
		for (j=0; j<v; j++, r++, v2++)
		{
			base_data[r] = v2 & 0xff;
		}
	}

	fileAllPathsAbsolute(true);
	fileDisableAutoDataDir();

	fileForceRemove("pigtest.hogg");
	hog_file = hogFileRead("pigtest.hogg", NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
	//hogFileSetSingleAppMode(hog_file, true);
	assert(hog_file);

	while (!kbhit())
	{
		int i=0;
		int index = randInt(1000);
		char name[MAX_PATH];
		unsigned char *data;
		int size;
		sprintf(name, "%d.con", index);
		size = 500+randInt(250000);
		data = malloc(size);
		memcpy(data, base_data, size);

		hogFileModifyUpdateNamed(hog_file, name, data, size, _time32(NULL), NULL);
		hogFileModifyUpdateNamed(hog_file, name, NULL, 0, 0, NULL);

		if (i++%100==0)
			printf(".");
	}
	hogFileDestroy(hog_file, true);

	// Verify
	hog_file = hogFileRead("pigtest.hogg", NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
	assert(hog_file);
	{
		char *sret=NULL;
		bool bret = hogFileVerifyToEstr(hog_file, &sret, false);
		assert(bret);
		estrDestroy(&sret);
	}
	hogFileDestroy(hog_file, true);


	timerFree(timer);
	exit(1);
}

int cmpStashElemStrIntPair(const void *_a, const void *_b)
{
	StashElement a = *(StashElement*)_a;
	StashElement b = *(StashElement*)_b;
	int d = stashElementGetInt(a) - stashElementGetInt(b);
	if (d!=0)
		return d;
	d = stricmp(stashElementGetStringKey(a), stashElementGetStringKey(a));
	return d;
}

void displayTable(StashTable st)
{
	StashElement *elems=NULL;
	int total=0;
	FOR_EACH_IN_STASHTABLE2(st, elem)
	{
		eaPush(&elems, elem);
		total += stashElementGetInt(elem);
	}
	FOR_EACH_END;
	eaQSort(elems, cmpStashElemStrIntPair);
	printf("\n");
	FOR_EACH_IN_EARRAY(elems, struct StashElementImp, elem)
	{
		if (stashElementGetInt(elem) < 50)
			break;
		printf("%80s : %6d (%1.1f%%)\n", stashElementGetStringKey(elem), stashElementGetInt(elem), stashElementGetInt(elem)*100.f/(float)total);
	}
	FOR_EACH_END;
	printf("\n");
}

void folderStats(void)
{
	int i, j;
	int count_filenames=0;
	int total_string_size=0;
	StashTable stExt = stashTableCreateWithStringKeys(1024, StashDeepCopyKeys_NeverRelease);
	StashTable stRoot = stashTableCreateWithStringKeys(1024, StashDeepCopyKeys_NeverRelease);
	StashTable stRoot2 = stashTableCreateWithStringKeys(1024, StashDeepCopyKeys_NeverRelease);
	StashTable stExtLen = stashTableCreateWithStringKeys(1024, StashDeepCopyKeys_NeverRelease);
	StashTable stRootLen = stashTableCreateWithStringKeys(1024, StashDeepCopyKeys_NeverRelease);
	StashTable stRoot2Len = stashTableCreateWithStringKeys(1024, StashDeepCopyKeys_NeverRelease);
	extern void setupDefaultPigsetFlags(void);
	SetAppGlobalType(GLOBALTYPE_CLIENT);
	setupDefaultPigsetFlags();
	FolderCacheChooseMode();
	fileLoadGameDataDirAndPiggs();
	FolderCacheReleaseHogHeaderData();
	for (i=0; i<PigSetGetNumPigs(); i++)
	{
		HogFile *hog = PigSetGetHogFile(i);
		for (j=0; j<(int)hogFileGetNumFiles(hog); j++)
		{
			const char *filename = hogFileGetFileName(hog, j);
			char *s;
			char buf[1024];
			int len;
			if (!filename || hogFileIsSpecialFile(hog, j))
				continue;
			count_filenames++;
			len = (int)strlen(filename) + 1;
			total_string_size += len;
			s = strrchr(filename, '.');
#define COUNT(st, key) {										\
						int count=0;							\
						stashFindInt(st, key, &count);			\
						count++;								\
						stashAddInt(st, key, count, true);		\
						count=0;								\
						stashFindInt(st##Len, key, &count);		\
						count+=len;								\
						stashAddInt(st##Len, key, count, true);	\
					}
			if (s)
			{
				COUNT(stExt, s);
			}
			strcpy(buf, filename);
			s = strchr(buf, '/');
			if (s)
			{
				*s = 0;
				COUNT(stRoot, buf);
				*s = '/';
				s = strchr(s+1, '/');
				if (s)
				{
					*s = 0;
					COUNT(stRoot2, buf);
				}
			}
#undef COUNT
		}
	}

	// Display results
	printf("%d filenames\n%s string size\n", count_filenames, friendlyBytes(total_string_size));
	displayTable(stExt);
	displayTable(stRoot);
	displayTable(stRoot2);
	printf("");
}

void testFolderCacheForceUpdate(void)
{
	char *filenames[] = {
		"shaders/D3D/D3D.MaterialProfile",
		"TestScripts/Aggro_Test.txt",
		"no_exist.txt",
		"texture_library/test/default_dxt5nm_N.texopt",
	};
	int i;

	extern void setupDefaultPigsetFlags(void);
	//SetAppGlobalType(GLOBALTYPE_CLIENT);
	//setupDefaultPigsetFlags();
	FolderCacheChooseMode();
	fileLoadGameDataDirAndPiggs();
	FolderCacheReleaseHogHeaderData();

	do 
	{
		int j;
		FolderCacheQuery(NULL, NULL);
		for (j=0; j<2; j++)
		{
			if (j==0)
				printf("Start/FolderCacheQuery:\n");
			else
				printf("FolderCacheForceUpdate:\n");

			for (i=0; i<ARRAY_SIZE(filenames); i++)
			{
				char buf[1024];
				__time32_t t;
				FolderNode *node;
				if (j==1)
					FolderCacheForceUpdate(folder_cache, filenames[i]);
				t = fileLastChanged(filenames[i]);
				_ctime32_s(SAFESTR(buf), &t);
				if (buf[0])
					buf[strlen(buf)-1] = '\0';
				node = FolderCacheQuery(folder_cache, filenames[i]);
				printf("  %s: %s", filenames[i], t?buf:"Doesn't exist");
				if (node)
				{
					printf("   virtual_location: %d\n", node->virtual_location);
				} else {
					printf("\n");
				}
			}
		}
		getch();
	} while (true);
}

void testFileCache()
{
	int i;
	for (i=0; i<2000; i++)
	{
		globCmdParse("fileCacheTimestampDump");
		fileCacheGetTimestamp("C:/temp/1.txt");
		fileCacheGetTimestamp("C:/temp/2.txt");
		fileCacheGetTimestamp("C:/temp/3.txt");
		globCmdParse("fileCacheTimestampDump");
		fileCacheTimestampStopMonitoring("C:/temp/1.txt");
		fileCacheTimestampStopMonitoring("C:/temp/3.txt");
		globCmdParse("fileCacheTimestampDump");
		fileCacheTimestampStopMonitoring("C:/temp/2.txt");
		globCmdParse("fileCacheTimestampDump");
		globCmdParse("mmpl");
		_getch();
	}
}

volatile U32 *mutex_comm;

void testMutexAbandonment1(void)
{
	extern int global_hack_hogg_exit_after_filelist_resize;
	int i=0;
	HogFile *hog_file;
	assertmsg(*mutex_comm == 0, "current step too high - old process still around?");
	*mutex_comm = 0;
	// open hogg
	hog_file = hogFileRead("pigtest.hogg", NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
	assert(hog_file);
	printf("[step 1, proccess 1]: open\n");
	// wait for #2 open
	printf("Waiting until [step 2, process 2] is complete\n");
	while (*mutex_comm != 2)
		Sleep(1);
	// grab mutex
	hogFileLock(hog_file);
	printf("[step 3, process 1]: grab mutex\n");
	*mutex_comm = 3;
	// wait
	printf("Waiting until [step 4, process 2] is started\n");
	while (*mutex_comm != 4)
		Sleep(1);
	Sleep(1000); // Must be absolutely sure it's actually stated waiting for test to be valid

	// start adding files until data list resizes
	// exit after writing filelist resize but without ever incrementing semaphore
	global_hack_hogg_exit_after_filelist_resize = 1;
	printf("[step 5, process 1]: Adding files until we make a significant header change...\n");
	do 
	{
		char fn[MAX_PATH];
		sprintf(fn, "process_1_file_%d.txt", i);
		hogFileModifyUpdateNamedSync(hog_file, fn, _strdup(fn), strlen(fn), time(NULL), NULL);
		i++;
	} while (true);
}

void testMutexAbandonment2(void)
{
	HogFile *hog_file;
	assertmsg(*mutex_comm < 2, "current step too high - old process still around?");
	// open hogg
	hog_file = hogFileRead("pigtest.hogg", NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
	assert(hog_file);
	printf("[step 2, proccess 2]: open\n");
	*mutex_comm = 2;
	// wait for #1 to grab mutex
	printf("Waiting until [step 3, process 1] is complete\n");
	while (*mutex_comm != 3)
		Sleep(1);
	// try to add a new file
	printf("[step 4, process 2]: start a modification (should not complete immediately)\n");
	printf("                        if broken, there will be no reload happening here\n");
	*mutex_comm = 4;
	hogFileModifyUpdateNamedSync(hog_file, "test.txt", strdup("test.txt"), strlen("test.txt"), time(NULL), NULL);
	// If broken, expected no reload here
	printf("                        completed\n");
	// close and flush hogg
	hogFileDestroy(hog_file, true);
	printf("[step 6, process 2]: Closed hogg\n");

	printf("Now, if there is a bug, the hogg file is corrupt, verifying...\n");
	// try to open, will be bad if there is a bug
	hog_file = hogFileRead("pigtest.hogg", NULL, PIGERR_ASSERT, NULL, HOG_READONLY);
	assert(hog_file);
	{
		char *sret=NULL;
		bool bret = hogFileVerifyToEstr(hog_file, &sret, false);
		printf("%s\n", sret);
		assert(bret);
		estrDestroy(&sret);
	}
	hogFileDestroy(hog_file, true);
	printf("[step 7, process 2]: Verify complete\n");
	_getch();
}

void testMutexAbandonment(void)
{
	mutex_comm = sharedMemoryDebugGetScratch("PigLibTestMutexComm", sizeof(U32));
	fileAllPathsAbsolute(true);
	hog_verbose = 1;
	printf("Note: this only works if HOGG_WATCH_TIMESTAMPS is 0 in hoglib.c *or* this entire process\n"
		   "  happens less than one second after the previous modification to pigtest.hogg (not likely).\n\n");
	printf("Is this the first process? [y/N]");
	if (consoleYesNo())
		testMutexAbandonment1();
	else
		testMutexAbandonment2();
}

void testReadOnlyThenWritable(void)
{
	HogFile *hog_file;
	HogFile *hog_file2;
	hog_file = hogFileRead("pigtest.hogg", NULL, PIGERR_ASSERT, NULL, HOG_READONLY);

	if (0)
	{
		// This code, combined with putting a Sleep(1000) in hoglib.c's handling of HFM_RELEASE_MUTEX,
		// reproduced a deadlock in hogFileReloadAsWritable().
		U32 datasize;
		HogFileIndex file_index;
		file_index= hogFileFind(hog_file, "AILib/aiAggro.c");
		assert(file_index != HOG_INVALID_INDEX);
		hogFileExtract(hog_file, file_index, &datasize, NULL);
	}

	hog_file2 = hogFileRead("pigtest.hogg", NULL, PIGERR_ASSERT, NULL, HOG_MUST_BE_WRITABLE);

	assert(hog_file == hog_file2);

	hogFileDestroy(hog_file2, true);
	hogFileDestroy(hog_file, true);
}

int wmain(int argc, WCHAR** argv_wide) {
	char buf[MAX_PATH]={0};
	unsigned char *mem;
	int count;
	DO_AUTO_RUNS

	disableRtlHeapChecking(NULL);
	cryptMD5Init();

	//extractDataFromCorruptPigStuff();
	//testCreateThenDelete();
	//simpleObjectDBtest();
	//folderStats();
	//testThreadedQueuedDeletes();
	//testUpdateThenDelete();
	//testFolderCacheForceUpdate();
	//testReadOnlyThenWritable();

	if (0) {
		extern void dynamicCacheLoadTest(void);
		dynamicCacheLoadTest();
	}
	//testOpenClose2();

	printf("Do you want to test mutex abandonment? [y/N]");
	if (consoleYesNo()) {
		testMutexAbandonment();
		return;
	}

	FolderCacheSetMode(FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC);

	//testThreadedPigLoading();

//	printf("Do you want to to test the FolderMonitor? [y/N]");
//	if (consoleYesNo()) {
//		folderMonTest2();
//		return;
//	}

// 	printf("Do you want to stress test async hogg writing? [y/N]");
// 	if (consoleYesNo()) {
// 		stressTestHoggWriting();
// 		return;
// 	}

	// testFileCache();

	loadstart_printf("Loading game piggs...");
	fileLoadGameDataDirAndPiggs();
	loadend_printf("done.");


	//createThenDeleteTest();

	//deleteThenCreateTest();

	printf("Do you want to test multi-process locking of multiple hoggs? [y/N]"); // should have working directory of C:\core
	if (consoleYesNo()) {
		multiProcessHogLockTest();
		return;
	}

	printf("Do you want to test multi-process hog modification? [y/N]");
	if (consoleYesNo()) {
		multiProcessHogModTest();
		return;
	}

	while(true)
	{
		char locatebuf[MAX_PATH];
		static bool first=true;
		FolderNode *node;
		if (first) {
			strcpy(buf, "test/test1.txt");
			first = false;
		} else {
			char oldbuf[MAX_PATH];
			strcpy(oldbuf, buf);
			printf("Enter a filename: ");
			gets(buf);
			if (buf[0]==0)
				strcpy(buf, oldbuf);
		}
		if (buf[0]==0 || stricmp(buf, "exit")==0) break;
		if (fileLocateRead(buf, locatebuf)) {
			printf("Located: %s\n", locatebuf);
		} else {
			printf("Not found by fileLocateRead()\n");
		}
		node = FolderCacheQuery(folder_cache, buf);
		if (!node) {
			printf("File not found by FolderCacheQuery()!\n");
		} else {
			printf("FolderNode says: %s: %d bytes timestamp: %s, writeable: %d\n",
				(node->virtual_location>=0)?"FS":"Pigg",
				node->size, _ctime32(&node->timestamp), (int)(node->writeable));
		}
		FolderNodeLeaveCriticalSection();
		{
			mem = fileAlloc(buf, &count);
			if (mem==NULL) {
				printf("Error extracting file!\n");
			} else {
				if (!mem[0]) {
					printf("0-length file\n");
				} else {
					int i;
					printf("First %d bytes of file:\n\t", MIN(64, count));
					for (i=0; i<64 && i<count; i++) {
						if (isprint(mem[i]) || mem[i]=='\t') {
							printf("%c", mem[i]);
						} else if (mem[i] && strchr("\r\n", mem[i])) {
							printf("%c\t", mem[i]);
						} else {
							printf("%02Xh ", mem[i]);
						}
					}
					printf("\n");
				}

				// Test functions:
				if (count>16) {
					FILE *f = fileOpen(buf, "rb");
					if (!f) {
						printf("Error opening file!\n");
					} else {
						U8 data[32];
						U8 data2[16];
						assert(ftell(f)==0);
						assert(15==fread(data, 1, 15, f));
						assert(ftell(f)==15);
						assert(data[0]==mem[0] && data[1]==mem[1] && data[2]==mem[2] && data[3]==mem[3] && data[13] == mem[13]); // Check for reading from cached header == reading the whole file
						assert(0==fseek(f, -4, SEEK_CUR));
						assert(ftell(f)==11);
						assert(3==fread(data2, 1, 3, f));
						assert(data2[0]==data[11] && data2[1]==data[12] && data2[2]==data[13]);
						assert(fgetc(f) == data[14]);
						assert(0==fseek(f, 0, SEEK_SET));
						assert(ftell(f)==0);
						assert(0==fseek(f, -2, SEEK_END));
						assert(ftell(f)==fileGetSize(f)-2);
						fclose(f);
					}
				}
				fileFree(mem);
				// Test fgets
				{
					FILE *f = fileOpen(buf, "r");
					if (!f) {
						printf("Error opening file!\n");
					} else {
						char data[128];
						fgets(data, 125, f);
						printf("Read: '%s'\n", data);
						fclose(f);
					}
				}
			}
		}
		// General file function tests
		printf("fileExists: %d\n", fileExists(buf));
		{
			char buf2[MAX_PATH];
			printf("fileLocateRead: %s\n", fileLocateRead(buf, buf2));
			printf("fileLocateWrite: %s\n", fileLocateWrite(buf, buf2));
		}
		printf("\n");


	}
	return 0;
}


int autoStruct_fixup_PigEntryInfo(void)
{
	return 0;
}
