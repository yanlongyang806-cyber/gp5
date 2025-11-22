#include "gimmeDLLWrapper.h"
#include "timing.h"
#include "file.h"
#include "utils.h"
#include "strings_opt.h"
#include "EString.h"
#include "globals.h"
#include "fileutil2.h"


int CreateRequest(char **files, int numFiles, char *comment)
{
	FILE *requestFile = NULL;
	FILE *requestLock = NULL;
	char fullpath[256] = {0};
	char fullpathLock[256] = {0};
	char date[256] = {0};
	char newComment[MAX_COMMENT_LINE] = {0};
	int i;

	timerMakeDateString(date);
	// colons arent allowed in file names
	strchrReplace(date, ':', '-');
	sprintf(fullpath, "%s/%s.%s", requestPath, gimmeDLLQueryUserName(), date);
	sprintf(fullpathLock, "%s.lock", fullpath);

	// the request file they are trying to make already exists
	if ( fileExists(fullpathLock) || !(requestLock = fopen(fullpathLock, "w")) || (requestFile = fopen(fullpath, "r!")) )
	{
		printfColor(COLOR_RED|COLOR_BRIGHT, "Cannot create request: This request already exists or is currently in use.\n");
		fclose(requestFile);
		return 0;
	}
	mkdirtree(fullpath);
	
	requestFile = fopen(fullpath, "wt");
	if ( !requestFile )
	{
		printfColor(COLOR_RED|COLOR_BRIGHT, "Cannot create request: Could not open %s", fullpath);
		return 0;
	}

	for ( i = 0; i < numFiles; ++i )
	{
		printf( "Adding file to request: %s\n", files[i]);
		fprintf(requestFile, "FILE %s\n", files[i]);
	}
	strncpy(newComment, comment, MAX_COMMENT_LINE-1);
	// get rid of newlines in the comment
	strchrReplace(newComment, '\n', ' ');
	printf( "Adding comment to request: %s\n", comment);
	fprintf(requestFile, "COMMENT %s\n", comment);

	fclose(requestFile);
	fclose(requestLock);
	if ( fileForceRemove(fullpathLock) != 0 )
	{
		printfColor(COLOR_RED|COLOR_BRIGHT, "Could not remove lock on %s\n", fullpath);
		return 0;
	}
	return 1;
}

int fileReadLine(char *buff, int size, FILE *file)
{
	char c = 0;
	int pos = 0;
	while ( c != '\n' )
	{
		int read = fread(&c, 1, sizeof(char), file);
		if ( read == 0 )
			break;
		buff[pos++] = c;
		if ( pos >= size )
			break;
	}
	buff[pos] = 0;
	return pos == 0 ? 0 : 1;
}

int CompileMasterRequestList()
{
	char **files;
	//char *origMasterList = NULL;
	char *masterList = NULL;
	int masterListLen = 0;
	char modifiedFileName[512];
	char lockFileName[256];
	//char who[256];
	char line[MAX_COMMENT_LINE];
	FILE *requestFile = NULL, *masterListFile = NULL;
	int count, i;

	strcpy(modifiedFileName, masterRequestListPath);
	mkdirtree(modifiedFileName);
	estrCreate(&masterList);

	//origMasterList = fileAlloc(masterRequestListPath, &masterListLen);
	//if ( origMasterList )
	//	estrAppend2(&masterList, origMasterList);

	if ( !LockMasterRequestList() )
		return 0;

	files = fileScanDir(requestPath, &count);
	for ( i = 0; i < count; ++i )
	{
		char *datePtr, *temp;
		char *namePtr;
		strcpy(modifiedFileName, files[i]);
		sprintf(lockFileName, "%s.lock", files[i]);
		printf("Processing File: %s\n", files[i]);
		
		// dont process lock files
		if ( strEndsWith(files[i], ".lock") )
			continue; 

		if ( fileExists(lockFileName) ||
			!fileExists(modifiedFileName) ||
			!(requestFile = fopen(modifiedFileName, "rt"))
			/*|| !(requestFile =  _sopen(modifiedFileName, _O_RDWR, _SH_DENYRW)) <=0*/ 
			)
		{
			printfColor(COLOR_RED|COLOR_BRIGHT, "Cannot add %s to Master Request List: file is in use.\n", files[i]);
			continue;
		}
		datePtr = strchr(modifiedFileName, '.');
		if ( !datePtr )
		{
			printfColor(COLOR_RED|COLOR_BRIGHT, "Cannot add %s to Master Request List: file format is incorrect.\n", files[i]);
			fclose(requestFile);
			continue;
		}
		namePtr = datePtr;
		// seperate the name string from the date string
		*datePtr = 0;
		++datePtr;
		temp = datePtr;
		// chop off the time portion of the date
		while ( *temp && *temp != ' ' && *temp != '_' ) ++temp;
		*temp = 0;
		// find the start of the name
		while ( namePtr && *namePtr != '/' && *namePtr != '\\' ) --namePtr;
		++namePtr;
		estrConcatf(&masterList, "\n\nREQUEST\n{\n");
		estrConcatf(&masterList, "\tWHO %s\n", namePtr);
		estrConcatf(&masterList, "\tDATE %s\n", datePtr);
		while ( fileReadLine(line, MAX_COMMENT_LINE, requestFile) )
			estrConcatf(&masterList, "\t%s", line);
		estrConcatf(&masterList, "}\n");

		// delete the file so that it doesnt get reprocessed in the future
		if ( fclose(requestFile)!= 0 || fileForceRemove(files[i]) != 0 )
		{
			printfColor(COLOR_RED|COLOR_BRIGHT, "Could not remove file: %s (ERROR: %d).  That is probably a bad thing.\n", files[i], GetLastError());
		}
	}

	//remove(masterRequestListPath);
	masterListFile = fopen( masterRequestListPath, "at" );
	fwrite(masterList, 1, strlen(masterList), masterListFile);
	fclose(masterListFile);
	if ( !UnlockMasterRequestList() )
		return 0;
	estrDestroy(&masterList);

	return 1;
}