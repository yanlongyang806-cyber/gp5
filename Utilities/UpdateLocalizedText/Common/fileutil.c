#include "fileutil.h"
#include "HashTable.h"
#include "StringTable.h"

// FIXME!!!
// Probably not such a great idea to grab an extern this way.
extern StringTable gameDataDirs;

static HashTable processedFiles = 0;
static char* curRootPath;
static int curRootPathLength;
static FileScanProcessor scanAllDataDirsProcessor;
static char* scanTargetDir;

static void fileScanAllDataDirsRecurseHelper(char* relRootPath){
	struct _finddata_t fileinfo;
	int	handle,test;
	char buffer[1024];
	FileScanAction action = FSA_EXPLORE_DIRECTORY;

	sprintf(buffer, "%s/*", relRootPath);
	
	for(test = handle = _findfirst(buffer, &fileinfo);test >= 0; test = _findnext(handle, &fileinfo)){
		if(fileinfo.name[0] == '.')
			continue;
		
		// Check if the file has already been processed before.
		//	Construct the relative path name to the file.
		sprintf(buffer, "%s/%s", relRootPath + curRootPathLength, fileinfo.name);
		
		if(fileinfo.attrib & _A_SUBDIR){
			// Send directories to the processor directory without caching the name.
			// We do not want to prevent duplicate directories from being explored,
			// only duplicate files.
			action = scanAllDataDirsProcessor(relRootPath, &fileinfo);
		} else {
			//	Check if the path exists in the table of processed files.
			if(!hashFindValue(processedFiles, buffer)){
				// The file has not been processed.  
				// Process it and then add it to the processed files table.
				action = scanAllDataDirsProcessor(relRootPath, &fileinfo);

				hashAddElement(processedFiles, buffer, (void*)1);
			}
		}

		

		if(	action & FSA_EXPLORE_DIRECTORY && 
			fileinfo.attrib & _A_SUBDIR){

			sprintf(buffer, "%s/%s", relRootPath, fileinfo.name);
			fileScanAllDataDirsRecurseHelper(buffer);
		}

		if(action & FSA_STOP)
			break;

		
	}
	_findclose(handle);
}

static int fileScanAllDataDirsHelper(char* rootPath){
	char buffer[1024];

	sprintf(buffer, "%s/%s", rootPath, scanTargetDir);
	curRootPath = buffer;
	curRootPathLength = strlen(buffer);
	fileScanAllDataDirsRecurseHelper(buffer);

	// Always continue through the string table.
	return 1;
}


void fileScanAllDataDirs(char* dir, FileScanProcessor processor){
	scanTargetDir = dir;

	// Create or clear the cache of files that has been seen by the
	// directory scanner.
	if(!processedFiles){
		processedFiles = createHashTable();
		initHashTable(processedFiles, 32);
		hashSetMode( processedFiles, FullyAutomatic | CaseInsensitive );
	}else{
		clearHashTable(processedFiles);
	}
	
	scanAllDataDirsProcessor = processor;

	// Scan all known data directories.
	strTableForEachString(gameDataDirs, fileScanAllDataDirsHelper);
}