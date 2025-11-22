#include "file.h"
#include "piglib.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include "mathutil.h"
#include <ctype.h>
#include <string.h>

#include "FolderCache.h"
#include <conio.h>
#include "wininclude.h"
#include "fileutil.h"


extern int folder_cache_debug;

static void pak() {
#ifdef _XBOX
#else
	int c;
	printf("Press any key to continue...\n");
	c = _getch();
#endif
}

FileScanAction printLocation(char* dir, struct _finddata_t* data){
	char filename[MAX_PATH];
	sprintf(filename, "%s/%s", dir, data->name);
	printf("%s  : ", filename);
	printf("%s\n", fileLocateRead(filename, filename));
	return FSA_EXPLORE_DIRECTORY;
}

extern FolderCache *folder_cache; // in file.c

void folderMonTest(void) {
	char temp[MAX_PATH] = {0};
	//FolderCache *fc = FolderCacheCreate();

	folder_cache_debug = 2;
	
	//FolderCacheSetMode(FOLDER_CACHE_MODE_I_LIKE_PIGS);

	//FolderCacheAddFolder(fc, "c:/game/data/", 0);
	//FolderCacheAddFolder(fc, "c:/game_edit/data/", 1);
	//FolderCacheAddAllPigs(fc);
	FolderCacheQueryEx(NULL, "", true, false); // Just to get the folder cache going

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_ALL, "*.texture", printFilenameCallback);

	while (true) {
		FolderNode *node;
		Sleep(1000);
		printf(".");
		node = FolderCacheQuery(folder_cache, "maps/hyb01.txt");
		//printf("%s\n", FolderCacheGetRealPath(fc, node, temp));
		//printf("\n");
		//fileScanAllDataDirs("scripts/contacts", printLocation);
		FolderNodeLeaveCriticalSection();
	}
	pak();

	//FolderCacheDestroy(fc);

	return;
}

void folderMonTest2(void) {
	FolderCache *fc = FolderCacheCreate();

	folder_cache_debug = 2;
	
	FolderCacheSetMode(FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC);
	printf("Loading directory tree...");
	FolderCacheAddFolder(fc, "c:/", 0, NULL, false);
	printf("done.\n");

	while (true) {
		FolderNode *node;
		Sleep(500);
		node = FolderCacheQuery(fc, "maps/hyb01.txt");
		FolderNodeLeaveCriticalSection();
	}
	pak();

	//FolderCacheDestroy(fc);

	return;
}
