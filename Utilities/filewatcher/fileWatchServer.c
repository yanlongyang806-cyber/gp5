#define _WIN32_IE 0x500
#include "conio.h"
#include "stdtypes.h"
#include "FolderCache.h"
#include "file.h"
#include "MemoryMonitor.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "utils.h"
#include "assert.h"
#include "EString.h"
#include "sysutil.h"
#include "winfiletime.h"
#include "fileWatch.h"
#include "MemoryPool.h"
#include "StashTable.h"
#include <sys/stat.h>
#include "strings_opt.h"
#include "winutil.h"
#include "mutex.h"
#include "sysutil.h"
#include <Tlhelp32.h>
#include "DirMonitor.h"
#include "RegistryReader.h"
#include "piglib.h"
#include "timing_profiler_interface.h"
#include "gimmeDLLWrapper.h"
#include "process_util.h"
#include "earray.h"
#include "UTF8.h"

// Icon colors.

#define ICON_LETTER_COLOR			0xff9020
#define ICON_LETTER_COLOR_SIMULATE	0x80ff80

// FILEWATCHER_VERSION is the number used to auto-start new versions of FileWatcher.
// If an older version is running, it will be killed by the new version when it starts.

#define FILEWATCHER_VERSION			12

static void notifyStatus(const char *title, const char *msg, U32 timeout);

typedef struct WatchedRoot WatchedRoot;

typedef struct WatchedFolder {
	FolderCache*		fc;
	char*				name;
	S32					nameLen;
	WatchedRoot*		root;
} WatchedFolder;

struct {
	WatchedFolder**		folder;
	S32					count;
	S32					maxCount;
} watchedFolders;

typedef struct WatchedRoot {
	FolderCache*		fc;
	char				name[4];
	HANDLE				handle;
	U8*					changeBuffer;
	U32					changeBufferSize;
	OVERLAPPED			overlapped;
	U32					dirMonCompletionHandle;
} WatchedRoot;

struct {
	WatchedRoot**		root;
	S32					count;
	S32					maxCount;
} watchedRoots;

typedef struct FolderScanProgress {
	FolderNode*			curNode;
	char				dirName[MAX_PATH];
	char				wildcard[MAX_PATH];
	U32					matchAll;
} FolderScanProgress;

typedef struct FileClient {
	HANDLE				hPipe;

	U8					inBufferData[1000];
	FileWatchBuffer		inBuffer;

	U8					outBufferData[1000];
	FileWatchBuffer		outBuffer;

	OVERLAPPED			overlapped;

	U32					protocolVersion;
	U32					processID;
	char*				processName;

	struct {
		FolderScanProgress**	scan;
		S32						count;
		S32						maxCount;
	} scans;
	
	U32					dirMonCompletionHandle;

	U32					needsConnect			: 1;
	U32					connected				: 1;
	U32					reading					: 1;
} FileClient;

struct {
	FileClient**		client;
	S32					count;
	S32					maxCount;
	S32					connectedCount;
	S32					activeScanCount;
	S32					untrackedScanCount;
	S32					handledMessageCount;
	S32					statCount;
	S32					unwatchedStatCount;
	S32					unfoundStatCount;
	U32					hasDisconnectedClient	: 1;
} fileClients;

struct {
	CRITICAL_SECTION		csClientList;
	StashTable				stats;
	S32						changingNodes;
	S32						totalFolderCount;
	S32						totalFileCount;
	S32						totalNodeCount;
	S32						printCreatesAndDeletes;
	S32						printStatUnfound;
	S32						printStatUnwatched;
	S32						showTitleBarStats;
	S32						doneWithStartup;
	S32						autoStart;
	S32						doRescan;
	S32						inputThreadCompletionHandle;
	FolderScanProgress**	scansAvailable;
} fileWatch;

static FileWatchBuffer* startMessageToClient(FileClient* client, U32 cmd){
	FileWatchBuffer* writeBuffer = &client->outBuffer;

	writeBuffer->curBytePos = writeBuffer->curByteCount = 0;

	fwWriteBufferU32(writeBuffer, cmd);

	return writeBuffer;
}

static void setFileClientNeedsConnect(FileClient* client){
	client->needsConnect = 1;
	fileClients.hasDisconnectedClient = 1;
}

static void destroyScanByIndex(FileClient* client, S32 index){
	assert(index >= 0 && index < client->scans.count);
	
	assert(fileClients.activeScanCount > 0);

	fileClients.activeScanCount--;

	assert(client->scans.count > 0);

	client->scans.count--;
	
	eaPush(&fileWatch.scansAvailable, client->scans.scan[index]);

	CopyStructsFromOffset(client->scans.scan+index, 1, client->scans.count - index);
}

static void fileClientDisconnected(FileClient* client){
	//printf("Disconnected: 0x%8.8x, %s\n", client, client->processName);
	
	if(client->connected){
		client->connected = 0;
		client->reading = 0;
		
		assert(fileClients.connectedCount > 0);

		fileClients.connectedCount--;

		estrDestroy(&client->processName);
	}
	
	while(client->scans.count){
		destroyScanByIndex(client, 0);
	}
		
	SAFE_FREE(client->scans.scan);
	ZeroStruct(&client->scans);

	DisconnectNamedPipe(client->hPipe);

	setFileClientNeedsConnect(client);
}

static void sendMessageToClient(FileClient* client){
	PERFINFO_AUTO_START_FUNC();
	
	if(	!WriteFile(	client->hPipe,
					client->outBuffer.buffer,
					client->outBuffer.curByteCount,
					&client->outBuffer.curByteCount,
					&client->overlapped)
		&&
		GetLastError() != ERROR_IO_PENDING)
	{
		fileClientDisconnected(client);
	}
	
	PERFINFO_AUTO_STOP();
}

static FolderScanProgress* addFolderScanProgress(	FileClient* client,
													const char* dirName,
													const char* wildcard,
													FolderNode* node)
{
	FolderScanProgress** scanPtr;
	FolderScanProgress* scan;

	scanPtr = dynArrayAddStruct(client->scans.scan,
								client->scans.count,
								client->scans.maxCount);

	scan = eaPop(&fileWatch.scansAvailable);
	
	if(!scan){
		scan = callocStruct(FolderScanProgress);
	}
	
	*scanPtr = scan;

	scan->curNode = node;
	Strncpyt(scan->dirName, dirName);
	Strncpyt(scan->wildcard, wildcard);
	
	if(!strcmp(wildcard, "*")){
		scan->matchAll = 1;
	}
	
	fileClients.activeScanCount++;

	return scan;
}

static void writeFindFileInfo(	FileWatchBuffer* writeBuffer,
								const char* fileName,
								__time32_t timeStamp,
								U32 size,
								S32 isDir,
								S32 writeable,
								S32 hidden,
								S32 system)
{
	FILETIME ft;
	U32 attributes;

	PERFINFO_AUTO_START("_UnixTimeToFileTime", 1);
	_UnixTimeToFileTime(timeStamp, &ft, FALSE);
	PERFINFO_AUTO_STOP();

	if(0){
		__time32_t temp;
		_FileTimeToUnixTime(&ft, &temp, FALSE);
		assert(temp == timeStamp);
	}

	fwWriteBufferString(writeBuffer, fileName);

	fwWriteBufferData(writeBuffer, &ft, sizeof(ft));
	
	fwWriteBufferU32(writeBuffer, size);
	
	attributes =	(isDir ? FILE_ATTRIBUTE_DIRECTORY : 0) |
					(writeable ? 0 : FILE_ATTRIBUTE_READONLY) |
					(hidden ? FILE_ATTRIBUTE_HIDDEN : 0) |
					(system ? FILE_ATTRIBUTE_SYSTEM : 0) |
					0;
	
	fwWriteBufferU32(writeBuffer, attributes);
}

static void writeNodeInfo(	FileWatchBuffer* writeBuffer,
							FolderNode* node)
{
	writeFindFileInfo(	writeBuffer,
						node->name,
						node->timestamp,
						(U32)node->size,
						node->is_dir,
						node->writeable,
						node->hidden,
						node->system);
}

static void scanWatchedFolderIfUnscanned(WatchedFolder* folder);

static void sendFindFirstFileReply(FileClient* client, FileWatchBuffer* readBuffer){
	char				fileSpec[MAX_PATH];
	FolderScanProgress* scan = NULL;
	FileWatchBuffer* 	writeBuffer;
	char				buffer[MAX_PATH];
	S32					i;
	S32					nodeIsContents = 0;
	
	Strncpyt(fileSpec, fwReadBufferString(readBuffer));
	forwardSlashes(fileSpec);

	// Get the directory name.

	Strncpyt(buffer, fileSpec);

	getDirectoryName(buffer);

	for(i = 0; i < watchedFolders.count; i++){
		WatchedFolder* folder = watchedFolders.folder[i];
		S32 len = (S32)strlen(folder->name);
		
		if(!stricmp(folder->name, buffer)){
			scanWatchedFolderIfUnscanned(folder);
			
			scan = addFolderScanProgress(client, buffer, getFileName(fileSpec), folder->fc->root);
			
			nodeIsContents = 1;
			
			break;
		}
		else if(!strnicmp(folder->name, fileSpec, len) &&
				isNullOrSlash(fileSpec[len]))
		{
			FolderNode* node;
			
			scanWatchedFolderIfUnscanned(folder);

			node = FolderCacheQuery(folder->fc, buffer + len);
			
			if(node){
				scan = addFolderScanProgress(client, buffer, getFileName(fileSpec), node);
			}
			FolderNodeLeaveCriticalSection();

			break;
		}
	}

	writeBuffer = startMessageToClient(client, FWM_S2C_FIND_FIRST_FILE_REPLY);

	if(!scan){
		// No watched folder.
		
		fwWriteBufferU32(writeBuffer, 0);
	}else{
		U32 handle = *(U32*)&scan;

		fwWriteBufferU32(writeBuffer, handle);
		
		if(	!scan->matchAll &&
			!simpleMatchExact(scan->wildcard, "."))
		{
			// Wildcard doesn't include ".", so find the first match.
			
			if(!nodeIsContents){
				scan->curNode = scan->curNode->contents;
			}
			
			while(	scan->curNode &&
					!simpleMatch(scan->wildcard, scan->curNode->name))
			{
				scan->curNode = scan->curNode->next;
			}
			
			if(scan->curNode){
				fwWriteBufferU32(writeBuffer, 1);
				
				writeNodeInfo(writeBuffer, scan->curNode);
				
				scan->curNode = scan->curNode->next;
			}else{
				// Nothing matches.
				
				fwWriteBufferU32(writeBuffer, 0);
				
				assert(scan == client->scans.scan[client->scans.count - 1]);
				
				destroyScanByIndex(client, client->scans.count - 1);
			}
		}
		else if(scan->curNode || nodeIsContents){
			fwWriteBufferU32(writeBuffer, 1);
			
			writeFindFileInfo(	writeBuffer,
								".",
								nodeIsContents ? time(NULL) : scan->curNode->timestamp,
								nodeIsContents ? 0 : (U32)scan->curNode->size,
								1,
								0,
								0,
								0);
			
			if(!nodeIsContents){
				scan->curNode = scan->curNode->contents;
			}
		}
		else{
			// Folder is watched, but no files were found.
			
			fwWriteBufferU32(writeBuffer, 0);
		}
	}

	sendMessageToClient(client);
}

static FolderScanProgress* findScanProgress(FileClient* client, U32 handle, S32* index){
	S32 i;

	for(i = client->scans.count - 1; i >= 0; i--){
		if(handle == *(U32*)&client->scans.scan[i]){
			if(index){
				*index = i;
			}
			return client->scans.scan[i];
		}
	}

	return NULL;
}

static void sendFindNextFileReply(FileClient* client, U32 handle){
	FolderScanProgress* scan = findScanProgress(client, handle, NULL);
	FileWatchBuffer*	writeBuffer;

	writeBuffer = startMessageToClient(client, FWM_S2C_FIND_NEXT_FILE_REPLY);
	
	if(scan){
		// If there's a wildcard, keep skipping until the first match.
		
		while(	scan->curNode &&
				!scan->matchAll &&
				!simpleMatch(scan->wildcard, scan->curNode->name))
		{
			scan->curNode = scan->curNode->next;
		}
	}
	
	if(scan && scan->curNode){
		fwWriteBufferU32(writeBuffer, handle);
		
		writeNodeInfo(writeBuffer, scan->curNode);

		scan->curNode = scan->curNode->next;
	}else{
		fwWriteBufferU32(writeBuffer, 0);
	}

	sendMessageToClient(client);
}

static void sendFindCloseReply(FileClient* client, U32 handle){
	S32					index;
	FolderScanProgress* scan = findScanProgress(client, handle, &index);
	FileWatchBuffer*	writeBuffer;

	writeBuffer = startMessageToClient(client, FWM_S2C_FIND_CLOSE_REPLY);
	
	if(scan){
		destroyScanByIndex(client, index);
		
		fwWriteBufferU32(writeBuffer, handle);
	}else{
		fwWriteBufferU32(writeBuffer, 0);
	}

	sendMessageToClient(client);
}

static void sendStatReply(FileClient* client, FileWatchBuffer* readBuffer){
	char				filePath[MAX_PATH];
	FileWatchBuffer*	writeBuffer = startMessageToClient(client, FWM_S2C_STAT_REPLY);
	char				buffer[MAX_PATH];
	S32					i;
	FolderNode*			node = NULL;
	char*				fileName;
	S32					isRoot = 0;
	
	Strncpyt(filePath, fwReadBufferString(readBuffer));
	if (strchr(filePath, ':') != strrchr(filePath, ':'))
	{
		printf("Someone requested a stat on an invalid path: \"%s\"\n", filePath);
		// Bad format file
		fwWriteBufferU32(writeBuffer, 0);
	} else {

		forwardSlashes(filePath);

		strcpy(buffer, filePath);
		getDirectoryName(buffer);
		
		fileName = getFileName(filePath);

		FolderNodeEnterCriticalSection();
		
		while(strEndsWith(fileName, "/")){
			fileName[strlen(fileName) - 1] = 0;
		}
		
		for(i = 0; i < watchedFolders.count; i++){
			WatchedFolder* folder = watchedFolders.folder[i];
			S32 len = (S32)strlen(folder->name);

			if(	!strnicmp(folder->name, filePath, len) &&
				isNullOrSlash(filePath[len]))
			{
				if(filePath[len]){
					len += 1;
				}
				
				if(!filePath[len]){
					isRoot = 1;
				}else{
					scanWatchedFolderIfUnscanned(folder);
					
					node = FolderCacheQueryEx(folder->fc, filePath + len, false, false);
				}
				
				break;
			}
		}
		
		if(i == watchedFolders.count){
			//printf("Can't find: %s\n", filePath);
			
			fwWriteBufferU32(writeBuffer, 0);
			
			fileClients.unwatchedStatCount++;
			
			if(fileWatch.printStatUnwatched){
				printf("Unwatched stat: %s\n", filePath);
			}
		}else{
			fwWriteBufferU32(writeBuffer, 1);
		
			//for(; node; node = node->next){
			//	if(!stricmp(node->name, fileName)){
			//		break;
			//	}
			//}

			if(isRoot){
				fwWriteBufferU32(writeBuffer, 1);
				
				fwWriteBufferU32(writeBuffer, time(NULL));
				
				fwWriteBufferU32(writeBuffer, 0);
				
				fwWriteBufferU32(writeBuffer, _S_IFDIR);
			}
			else if(node){
				U32 attributes =	(node->is_dir ? 0 : _S_IFREG) |
									_S_IREAD |
									(node->is_dir ? _S_IFDIR : 0) |
									(node->writeable ? _S_IWRITE : 0) |
									0;
				
				fwWriteBufferU32(writeBuffer, 1);
				
				fwWriteBufferU32(writeBuffer, node->timestamp);
				
				fwWriteBufferU32(writeBuffer, (U32)node->size);
				
				fwWriteBufferU32(writeBuffer, attributes);
			}else{
				fwWriteBufferU32(writeBuffer, 0);

				fileClients.unfoundStatCount++;

				if(fileWatch.printStatUnfound){
					printf("Unfound stat: %s\n", filePath);
				}
			}
		}

		FolderNodeLeaveCriticalSection();
	}

	sendMessageToClient(client);
}

static void handleMessageFromClient(FileClient* client){
	FileWatchBuffer* readBuffer = &client->inBuffer;
	U32 cmd;

	PERFINFO_AUTO_START_FUNC();

	readBuffer->curBytePos = 0;

	cmd = fwReadBufferU32(readBuffer);
	
	fileClients.handledMessageCount++;

	switch(cmd){
		xcase FWM_C2S_CONNECT:{
			PERFINFO_AUTO_START("connect", 1);
			
			client->protocolVersion = fwReadBufferU32(readBuffer);

			client->processID = fwReadBufferU32(readBuffer);

			estrCopy2(&client->processName, fwReadBufferString(readBuffer));

			if(0){
				printf(	"Connect version %d from: %d:%s\n",
						client->protocolVersion,
						client->processID,
						client->processName);
			}

			startMessageToClient(client, FWM_S2C_READY_FOR_REQUESTS);

			sendMessageToClient(client);

			PERFINFO_AUTO_STOP();
		}

		xcase FWM_C2S_FIND_FIRST_FILE:{
			PERFINFO_AUTO_START("firstfile", 1);
			sendFindFirstFileReply(client, readBuffer);
			PERFINFO_AUTO_STOP();
		}

		xcase FWM_C2S_FIND_NEXT_FILE:{
			U32 handle = fwReadBufferU32(readBuffer);

			PERFINFO_AUTO_START("nextFile", 1);
			sendFindNextFileReply(client, handle);
			PERFINFO_AUTO_STOP();
		}

		xcase FWM_C2S_FIND_CLOSE:{
			U32 handle = fwReadBufferU32(readBuffer);
			
			PERFINFO_AUTO_START("close", 1);
			sendFindCloseReply(client, handle);
			PERFINFO_AUTO_STOP();
		}
		
		xcase FWM_C2S_STAT:{
			fileClients.statCount++;
			
			PERFINFO_AUTO_START("stat", 1);
			sendStatReply(client, readBuffer);
			PERFINFO_AUTO_STOP();
		}

		xdefault:{
			printf("%s[%d]: Unhandled command: %d\n", client->processName, client->processID, cmd);
		}
	}
	
	PERFINFO_AUTO_STOP();// FUNC.
}

static void handleClientIO(FileClient* client, U32 bytesTransferred){
	EnterCriticalSection(&fileWatch.csClientList);

	if(!client->connected){
		assert(!client->needsConnect);

		client->connected = 1;
		client->reading = 0;

		fileClients.connectedCount++;
		
		client->inBuffer.curByteCount = client->inBuffer.curBytePos = 0;
		client->outBuffer.curByteCount = client->outBuffer.curBytePos = 0;
	}

	if(client->reading){
		// Finished a read operation.

		client->inBuffer.curByteCount = bytesTransferred;

		handleMessageFromClient(client);
	}else{
		// Finished a WRITE.

		assert(bytesTransferred == client->outBuffer.curBytePos);

		client->outBuffer.curBytePos = 0;
		
		// Start a READ.

		PERFINFO_AUTO_START("ReadFile", 1);
		if(	!ReadFile(	client->hPipe,
						client->inBuffer.buffer,
						sizeof(client->inBufferData),
						&client->inBuffer.curByteCount,
						&client->overlapped)
			&&
			GetLastError() != ERROR_IO_PENDING)
		{
			fileClientDisconnected(client);
		}
		PERFINFO_AUTO_STOP();
	}

	client->reading = !client->reading;

	LeaveCriticalSection(&fileWatch.csClientList);
}

static void connectPipe(FileClient* client){
	if(!client->needsConnect){
		return;
	}
	
	assert(!client->connected);

	client->needsConnect = 0;

	if(!ConnectNamedPipe(client->hPipe, &client->overlapped)){
		U32 error = GetLastError();

		switch(error){
			xcase ERROR_PIPE_CONNECTED:{
				// This means that the client connected AFTER CreateNamedPipe and BEFORE ConnectNamedPipe.
				
				//printf("Pipe already connected!\n");
				
				handleClientIO(client, 0);
			}

			xcase ERROR_NO_DATA:{
				// This means that the client connected AFTER CreateNamedPipe and disconnected BEFORE ConnectNamedPipe.
				
				//printf("Client connection failed!\n");

				fileClientDisconnected(client);
			}
			
			xcase ERROR_IO_PENDING:{
				//printf("Success, connection is pending.\n");
				// Success, and connection is now pending.
			}

			xdefault:{
				//printf("Failed opening pipe: %d\n", error);
				//FatalErrorf("Can't connect on pipe: %d", error);
			}
		}
	}else{
		//printf("Connected!\n");
	}
}

static void connectDisconnectedPipes(void){
	if(fileClients.hasDisconnectedClient){
		S32 i;
		
		PERFINFO_AUTO_START_FUNC();

		fileClients.hasDisconnectedClient = 0;

		for(i = 0; i < fileClients.count; i++){
			FileClient* client = fileClients.client[i];

			connectPipe(client);
		}
		
		PERFINFO_AUTO_STOP();
	}
}

static void fileWatchClientCompletionCallback(DirMonCompletionPortCallbackParams* params){
	if(params->result){	
		// Some client IO is finished.
		
		handleClientIO(params->userData, params->bytesTransferred);
	}
	else if(params->aborted){
		FatalErrorf("FileWatcher client operation aborted!\n");
	}
	else if(params->userData){
		// A client has disconnected during IO.
		
		fileClientDisconnected(params->userData);
	}
}

static void createNewClient(void){
	if(fileClients.connectedCount == fileClients.count){
		FileClient** clientPtr;
		FileClient* client;
		
		PERFINFO_AUTO_START_FUNC();

		EnterCriticalSection(&fileWatch.csClientList);
		
		clientPtr = dynArrayAddStruct(fileClients.client, fileClients.count, fileClients.maxCount);
		client = *clientPtr = calloc(sizeof(*client), 1);

		ZeroStruct(client);

		client->inBuffer.buffer = client->inBufferData;
		client->inBuffer.maxByteCount = sizeof(client->inBufferData);

		client->outBuffer.buffer = client->outBufferData;
		client->outBuffer.maxByteCount = sizeof(client->outBufferData);

		client->hPipe = CreateNamedPipe_UTF8(fileWatchGetPipeName(),
										PIPE_ACCESS_DUPLEX |
											FILE_FLAG_OVERLAPPED,
										PIPE_TYPE_MESSAGE |
											PIPE_READMODE_MESSAGE |
											PIPE_WAIT,
										PIPE_UNLIMITED_INSTANCES,
										10000,
										10000,
										0,
										NULL);

		if(client->hPipe == INVALID_HANDLE_VALUE){
			FatalErrorf("Can't open named pipe (%s): %d", fileWatchGetPipeName(), GetLastError());
		}
		
		if(0){
			printf("Waiting for connection...");
			_getch();
			printf("Done!\n");
		}

		client->dirMonCompletionHandle = dirMonCompletionPortAddObject(NULL, client->hPipe, client, fileWatchClientCompletionCallback);

		setFileClientNeedsConnect(client);
		
		LeaveCriticalSection(&fileWatch.csClientList);
		
		PERFINFO_AUTO_STOP();
	}
}

static WatchedFolder* getWatchedFolderFromFolderCache(FolderCache* fc){
	S32 i;
	
	for(i = 0; i < watchedFolders.count; i++){
		if(watchedFolders.folder[i]->fc == fc){
			return watchedFolders.folder[i];
		}
	}
	
	return NULL;
}

static void addPath(char** outPath, FolderNode* node, int iPathBufferLen){
	char *startPath = *outPath;
	if(!node){
		return;
	}

	addPath(outPath, node->parent, iPathBufferLen);
	iPathBufferLen -= (*outPath - startPath);

	strcpy_s((*outPath)++, iPathBufferLen--, "/");
	strcpy_s(*outPath, iPathBufferLen, node->name);
	*outPath += strlen(*outPath);
}

static void printTotalsInTitleBar(void){
	char buffer[200];
	
	STR_COMBINE_SSSSS(	buffer,
						"FileWatcher: ",
						getCommaSeparatedInt(fileWatch.totalFolderCount),
						" folders, ",
						getCommaSeparatedInt(fileWatch.totalFileCount),
						" files");
						
	setConsoleTitle(buffer);
}

//#define USE_NAME_CHECK 1

#if USE_NAME_CHECK
	static StashTable nameCheck;

	static char* getFullName(FolderNode* node, char* buffer, char** buffer2){
		char* bufferOrig = buffer;
		
		if(node){
			getFullName(node->parent, NULL, buffer ? &buffer : buffer2);
			
			if(buffer){
				strcpy(buffer, node->name);
			}else{
				strcpy(*buffer2, node->name);
				*buffer2 += strlen(*buffer2);
				strcpy(*buffer2, "/");
				*buffer2 += 1;
			}
		}
		
		return bufferOrig;
	}
#endif

static void folderNodeCreateCallback(FolderCache* fc, FolderNode* node){
	U32 curTime = timeGetTime();
	
	#if USE_NAME_CHECK
	{
		char buffer[MAX_PATH];
		
		if(!nameCheck){
			nameCheck = stashTableCreateWithStringKeys(1000, StashDeepCopyKeys_NeverRelease);
		}
		
		assert(stashAddPointer(nameCheck, getFullName(node, buffer, NULL), NULL, false));
	}
	#endif
		
	if(node->is_dir){
		fileWatch.totalFolderCount++;
	}else{
		fileWatch.totalFileCount++;
	}
	
	fileWatch.totalNodeCount++;
	
	if(fileWatch.changingNodes){
		static U32 lastTime;
	
		if(curTime - lastTime > 100){
			lastTime = curTime;
			
			printTotalsInTitleBar();
		}
	}
	else if(fileWatch.printCreatesAndDeletes){
		char fullPath[MAX_PATH];
		char* curPos;
		
		strcpy(fullPath, fc->gamedatadirs[0]);
		
		curPos = fullPath + strlen(fullPath);
		
		addPath(&curPos, node, MAX_PATH - (int) strlen(fullPath));
		
		printf("Created: %s\n", fullPath);

		//_stat(fullPath, &info->info);
	}
}

static void folderNodeDeleteCallback(FolderCache* fc, FolderNode* node){
	U32 curTime = timeGetTime();

	#if USE_NAME_CHECK
	{
		char buffer[MAX_PATH];
		assert(nameCheck);
		assert(stashRemovePointer(nameCheck, getFullName(node, buffer, NULL), NULL));
	}
	#endif

	if(node->is_dir){
		fileWatch.totalFolderCount--;
	}else{
		fileWatch.totalFileCount--;
	}

	assert(fileWatch.totalNodeCount > 0);
	
	fileWatch.totalNodeCount--;
		
	if(fileWatch.changingNodes){
		static U32 lastTime;
	
		if(curTime - lastTime > 100){
			lastTime = curTime;
			
			printTotalsInTitleBar();
		}
	}
	else if(fileWatch.printCreatesAndDeletes){
		WatchedFolder* wf = getWatchedFolderFromFolderCache(fc);
		char fullPath[MAX_PATH];
		char* curPos;
		
		strcpy(fullPath, fc->gamedatadirs[0]);
		
		curPos = fullPath + strlen(fullPath);
		
		addPath(&curPos, node, MAX_PATH - (int) strlen(fullPath));

		printf("Deleted: %s\n", fullPath);
	}
}

static void scanWatchedFolder(WatchedFolder* folder){
	char status[128];
	if(folder->fc){
		loadstart_printf("  Destroying cache \"%s\"... ", folder->name);

		fileWatch.changingNodes = 1;
		
		FolderCacheDestroy(folder->fc);
		
		fileWatch.changingNodes = 0;

		loadend_printf("done!");
	}

	sprintf(status, "Caching \"%s\"...", folder->name);
	notifyStatus("FileWatcher", status, 30);

	loadstart_printf("  Adding cache \"%s\"... ", folder->name);

	// Create the FolderCache.

	folder->fc = FolderCacheCreate();
	
	folder->fc->nodeCreateCallback = folderNodeCreateCallback;
	folder->fc->nodeDeleteCallback = folderNodeDeleteCallback;

	fileWatch.changingNodes = 1;
	
	FolderCacheSetFullScanCreatedFolders(folder->fc, 1);
	
	if(dirExists(folder->name)){
		FolderCacheAddFolder(folder->fc, folder->name, 0, NULL, false);
	}else{
		//printf("Folder doesn't exist: %s\n", folder->name);
	}
	
	fileWatch.changingNodes = 0;

	loadend_printf("done!");
	sprintf(status, "\"%s\" complete!", folder->name);
	notifyStatus("FileWatcher", status, 10);
}

static void scanWatchedFolderIfUnscanned(WatchedFolder* folder){
	if(!folder->fc){
		scanWatchedFolder(folder);
	}
}

static void scanWatchedFolders(void){
	S32 i;
	
	for(i = 0; i < watchedFolders.count; i++){
		WatchedFolder* folder = watchedFolders.folder[i];
		
		scanWatchedFolder(folder);
	}
}

static void startWatchingRoot(WatchedRoot* root){
	static U32 unused;
	
	U32 errorStart = GetLastError();
	
	ZeroStruct(&root->overlapped);
	
	if(!ReadDirectoryChangesW(	root->handle,						// handle to directory
								root->changeBuffer,					// read results buffer
								root->changeBufferSize,				// length of buffer
								1,									// monitor the whole tree
								FILE_NOTIFY_CHANGE_DIR_NAME,		// filter conditions
								&unused,							// bytes returned
								&root->overlapped,					// overlapped buffer
								NULL))								// completion routine
	{
		U32 error = GetLastError();
		
		FatalErrorf("Can't create directory change notification for: %s, error %d\n", root->name, error);
	}else{
		U32 error = GetLastError();
		
		//printf("Created directory change notification for: %s, error %d (from %d)\n", root->name, error, errorStart);
	}
}

#include "winfiletime.h"

static S32 recursiveScan(	S32				useFileWatcher,
							const char*		path,
							S32				printNames,
							S32* 			fileCount,
							S32* 			folderCount,
							S32 			canCancel,
							S32 			doVerify,
							S32 			doStat,
							const char*		matchFile)
{
	WIN32_FIND_DATAA wfd;
	char			buffer[MAX_PATH];
	U32				fwHandle = -1; // this is what fwFindFirstFile returns in fwHandle for 'invalid'
	S32				good;
	HANDLE			diskHandle = INVALID_HANDLE_VALUE;
	
	if(canCancel && _kbhit()){
		_getch();
		return 0;
	}
	
	STR_COMBINE_SS(buffer, path, "/*");

	for(good = useFileWatcher ? fwFindFirstFile(&fwHandle, buffer, &wfd) && fwHandle : (diskHandle = FindFirstFile_UTF8(buffer, &wfd)) != NULL;
		good;
		good = useFileWatcher ? fwFindNextFile(fwHandle, &wfd) : FindNextFile_UTF8(diskHandle, &wfd))
	{
		if(	!strcmp(wfd.cFileName, ".") ||
			!strcmp(wfd.cFileName, ".."))
		{
			continue;
		}
		
		if(printNames){
			__time32_t utcTime;
			
			_FileTimeToUnixTime(&wfd.ftLastWriteTime, &utcTime, FALSE);
#pragma push_macro("printf")
#undef printf
			printf(	"%d, "
					#if 0
						"%"FORM_LL"d"
					#else
						"%d"
					#endif
					", %4d: %s/%s%s\n",
					fwHandle,
					#if 0
						wfd.ftLastWriteTime,
					#else
						utcTime,
					#endif
					wfd.dwFileAttributes,
					path,
					wfd.cFileName,
					wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? "/" : "");
#pragma pop_macro("printf")
		}
		else if(matchFile &&
				strstriConst(wfd.cFileName, matchFile))
		{
			printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,	
						"%s/%s%s\n",
						path,
						wfd.cFileName,
						wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? "/" : "");
		}
			
		if(doStat){
			FWStatType info;
			
			STR_COMBINE_SSS(buffer, path, "/", wfd.cFileName);

			fwStat(buffer, &info);
		}
		
		if(doVerify){
			FWStatType statInfo1;
			struct _stat32i64 statInfo2;
			S32 ret1;
			S32 ret2;
			
			STR_COMBINE_SSS(buffer, path, "/", wfd.cFileName);
			
			ret1 = fwStat(buffer, &statInfo1);
			
			ret2 = cryptic_stat32i64_utc(buffer, &statInfo2);
			
			if(ret1 != ret2){
				printf("%s%s\n", buffer, wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? "/" : "");
				consoleSetColor(COLOR_BRIGHT|COLOR_RED, 0);
				printf("   Mismatched return value: fw:%d vs disk:%d\n", ret1, ret2);
				consoleSetDefaultColor();
			}
			else if(!ret1){
				if((statInfo1.st_mode & _S_IFDIR) != (statInfo2.st_mode & _S_IFDIR)){
					printf("%s%s\n", buffer, wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? "/" : "");
					consoleSetColor(COLOR_BRIGHT|COLOR_RED, 0);
					printf("   Mismatched dir flag: fw:%d vs disk:%d\n", statInfo1.st_mode & _S_IFDIR, statInfo2.st_mode & _S_IFDIR);
					consoleSetDefaultColor();
				}

				if(	!(statInfo1.st_mode & _S_IFDIR) &&
					statInfo1.st_mtime != statInfo2.st_mtime)
				{
					printf("%s%s\n", buffer, wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? "/" : "");
					consoleSetColor(COLOR_BRIGHT|COLOR_RED, 0);
					printf("   Mismatched modified time: fw:%d vs disk:%d      (diff: %d)\n", statInfo1.st_mtime, statInfo2.st_mtime, statInfo1.st_mtime - statInfo2.st_mtime);
					consoleSetDefaultColor();
				}

				if(statInfo1.st_size != statInfo2.st_size){
					printf("%s%s\n", buffer, wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? "/" : "");
					consoleSetColor(COLOR_BRIGHT|COLOR_RED, 0);
					printf("   Mismatched size: fw:%d vs disk:%"FORM_LL"d\n", statInfo1.st_size, statInfo2.st_size);
					consoleSetDefaultColor();
				}

				if(	!(statInfo1.st_mode & _S_IFDIR) &&
					(statInfo1.st_mode & _S_IWRITE) != (statInfo2.st_mode & _S_IWRITE))
				{
					printf("%s%s\n", buffer, wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? "/" : "");
					consoleSetColor(COLOR_BRIGHT|COLOR_RED, 0);
					printf("   Mismatched read-only flag: fw:%d vs disk:%d\n", statInfo1.st_mode & _S_IWRITE, statInfo2.st_mode & _S_IWRITE);
					consoleSetDefaultColor();
				}
			}
		}

		if(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
			if(folderCount){
				(*folderCount)++;
			}
			
			STR_COMBINE_SSS(buffer, path, "/", wfd.cFileName);
			
			if(!recursiveScan(useFileWatcher, buffer, printNames, fileCount, folderCount, canCancel, doVerify, doStat, matchFile)){
				return 0;
			}
		}else{
			if(fileCount){
				(*fileCount)++;
			}
		}
	}
	
	if(useFileWatcher){
		fwFindClose(fwHandle);
	}else{
		FindClose(diskHandle);
	}
	
	return 1;
}

static S32 isGoodStringChar(char c){
	return	isalnum(c) ||
			strchr(" !@#$%^&*()-_=_[{]}\\|;:'\",<.>/?`~", c);
}

static S32 enterString(char* buffer, S32 maxLength){
	S32 length = 0;
	
	buffer[length] = 0;
	
	while(1){
		S32 key = _getch();

		switch(key){
			xcase 0:
			case 224:{
				_getch();
			}
			
			xcase 27:{
				while(length > 0){
					backSpace(1, 1);
					buffer[--length] = 0;
				}
				return 0;
			}
			
			xcase 13:{
				return 1;
			}
			
			xcase 8:{
				if(length > 0){
					backSpace(1, 1);
					buffer[--length] = 0;
				}
			}
			
			xcase 22:{
				if(OpenClipboard(compatibleGetConsoleWindow())){
					HANDLE handle = GetClipboardData(CF_TEXT);

					if(handle){
						char* data = GlobalLock(handle);
						
						if(data){
							while(	*data &&
									length < maxLength &&
									isGoodStringChar(*data))
							{
								buffer[length++] = *data;
								buffer[length] = 0;
								printf("%c", *data);
								
								data++;
							}
							
							GlobalUnlock(handle);
						}
					}

					CloseClipboard();
				}
			}
			
			xdefault:{
				if(	length < maxLength &&
					isGoodStringChar(key))
				{
					buffer[length++] = key;
					buffer[length] = 0;
					printf("%c", key);
				}
			}
		}
	}
}

static S32 runScanTestHelper(const char* path, S32 useFileWatcher, S32 doVerify, S32 doStat, const char* matchFile){
	S32 fileCount = 0;
	S32 folderCount = 0;
	S32 ret;
	
	loadstart_printf("Scanning %s: %s", path, matchFile || doVerify ? "\n" : "");
	ret = recursiveScan(useFileWatcher, path, 0, &fileCount, &folderCount, 1, doVerify, doStat, matchFile);
	loadend_printf("%s folders, %s files", getCommaSeparatedInt(folderCount), getCommaSeparatedInt(fileCount));
	
	return ret;
}

static void runScanTest(S32 useFileWatcher, S32 doVerify, S32 doStat, const char* matchFile){
	S32 ret = 1;
	
	#define PARAMS useFileWatcher, doVerify, doStat, matchFile

	ret = ret && runScanTestHelper("c:/StarTrek/data", PARAMS);
	ret = ret && runScanTestHelper("c:/StarTrek/tools", PARAMS);
	ret = ret && runScanTestHelper("c:/StarTrek/src", PARAMS);
	ret = ret && runScanTestHelper("c:/StarTrek/docs", PARAMS);
	
	ret = ret && runScanTestHelper("c:/fightclub/data", PARAMS);
	ret = ret && runScanTestHelper("c:/fightclub/tools", PARAMS);
	ret = ret && runScanTestHelper("c:/fightclub/src", PARAMS);
	ret = ret && runScanTestHelper("c:/fightclub/docs", PARAMS);
	
	#undef PARAMS
	
	if(!ret){
		printf("\n\nCANCELED!!\n\n");
	}
}

static void printCurrentFolder(void){
	char *pName = NULL;
	
	GetCurrentDirectory_UTF8(&pName);
	
	printf("Current directory: %s\n\n", pName);
}

static void testFileCreation(void){
	S32 i;
	
	for(i = 0; !_kbhit(); i++){
		char name[1000];
		FILE* f;
		char randomData[1000];
		S32 size = rand() % ARRAY_SIZE(randomData);
		
		sprintf(name, "c:\\game\\data\\temp\\%d.txt", i);
		
		f = fopen(name, "wb");
		
		fwrite(randomData, size, 1, f);
		
		fclose(f);
		
		assert(fileExists(name));
		assert(fileSize(name) == size);
		
		DeleteFile_UTF8(name);
		
		assert(!fileExists(name));
		
		printf("%d\n", i);
	}
	
	_getch();
	
	printf("Done!\n");
}

static S32 simulateClient(S32 noWait){
	S32 startTime = timeSecondsSince2000();
	S32 quit = 1;

	if(!noWait){
		printf("Press a key to start client simulator (ESC to cancel): ");

		while(timeSecondsSince2000() - startTime < 2){
			Sleep(1);

			if(_kbhit()){
				if(_getch() != 27){
					while(_kbhit()){
						_getch();
					}
					printf("STARTING!\n");
					quit = 0;
				}
				break;
			}
		}

		if(quit){
			printf("Canceled!\n");
			return 0;
		}
	}
	
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'F', ICON_LETTER_COLOR_SIMULATE);
	
	setConsoleTitle("FileWatch Client Simulator");

	if(0){
		void fileLoadDataDirs(S32 forceReload);

		fileLoadDataDirs(0);
	}

	if(1){
		S32 done = 0;
		
		while(!done){
			char buffer[1000];
			
			printf(	"\n"
					"ESC : Quit!\n"
					"  1 : Scan a folder AND verify against the disk.\n"
					"  2 : Stat a file/folder.\n"
					"  3 : Scan using FileWatcher.\n"
					"  4 : Scan using FileWatcher AND stat each file.\n" 
					"  5 : Scan using FileWatcher AND compare stats against the disk.\n"
					"  6 : Scan using disk, AND compare stats against FileWatcher.\n"
					"\n"
					"  S : Search for a filename.\n"
					"\n"
					"Enter selection: ");
					
					
			switch(_getch()){
				xcase '1':{
					printf("Scan\n\n");
					
					printCurrentFolder();
					
					printf("Enter folder to scan: ");
					
					if(!enterString(buffer, ARRAY_SIZE(buffer) - 1)){
						printf("Canceled!\n");
					}else{
						S32 folderCount = 0;
						S32 fileCount = 0;
						
						printf("\n");
						
						loadstart_printf("Scanning \"%s\":\n", buffer);

						recursiveScan(1, buffer, 1, &fileCount, &folderCount, 1, 1, 0, NULL);
						
						loadend_printf("%s folders, %s files", getCommaSeparatedInt(folderCount), getCommaSeparatedInt(fileCount));
					}
				}
				
				xcase '2':{
					printf("Stat\n\n");
					
					printCurrentFolder();
					
					printf("Enter file or folder to stat: ");
					
					if(!enterString(buffer, ARRAY_SIZE(buffer) - 1)){
						printf("Canceled!\n");
					}else{
						{
							FWStatType info;
							S32 ret = fwStat(buffer, &info);
						
							printf("\n");

							printf("FileWatcher:  ");
						
							if(ret == -1){
								printf("File doesn't exist!\n");
							}
							else if(!ret){
								printf(	"%9s %3s %14s %d\n",
										info.st_mode & _S_IWRITE ? "" : "READ-ONLY",
										info.st_mode & _S_IFREG ? "" : "DIR",
										getCommaSeparatedInt(info.st_size),
										info.st_mtime);
							}
							else{
								printf("Error?  ret = %d\n", ret);
							}
						}
						{
							struct _stat32i64 info;
							S32 ret = cryptic_stat32i64_utc(buffer, &info);
							
							printf("_stat:        ");
						
							if(ret == -1){
								printf("File doesn't exist!\n");
							}
							else if(!ret){
								printf(	"%9s %3s %14s %d\n",
										info.st_mode & _S_IWRITE ? "" : "READ-ONLY",
										info.st_mode & _S_IFREG ? "" : "DIR",
										getCommaSeparatedInt(info.st_size),
										info.st_mtime);
							}
							else{
								printf("Error?  ret = %d\n", ret);
							}
						}
					}
				}
				
				xcase '3':{
					printf("Running the FileWatcher scan test...\n\n");
					runScanTest(1, 0, 0, NULL);
				}
				
				xcase '4':{
					printf("Running the FileWatcher scan test with stat...\n\n");
					runScanTest(1, 0, 1, NULL);
				}

				xcase '5':{
					printf("Running the FileWatcher scan test with VERIFY...\n\n");
					runScanTest(1, 1, 0, NULL);
				}
				
				xcase '6':{
					printf("Running the disk scan test with VERIFY...\n\n");
					runScanTest(0, 1, 0, NULL);
				}
					
				//xcase 'f':{
				//	printf("Running file creation test...\n\n");
				//	testFileCreation();
				//}
				
				xcase 's':{
					char buffer2[1000];
					
					printf("Searching for a file...\n\n");
					
					printf("Enter a substring: ");
					
					if(!enterString(buffer2, ARRAY_SIZE(buffer2))){
						printf("Canceled!\n\n");
					}else{
						printf("\n\n");
						
						runScanTest(1, 0, 0, buffer2);
					}
				}
				
				xcase 27:{
					// ESC.
					printf("Quitting!\n\n");

					done = 1;
				}
				
				xdefault:{
					printf("Invalid selection\n\n");
				}
			}
		}
	}

	exit(0);
	
	return 1;
}

static void handleRootNotification(DirMonCompletionPortCallbackParams* params){
	static char		lastRename[MAX_PATH];
	static S32		lastRenameWatched;
	static S32		count;
	
	WatchedRoot*	root = params->userData;
	
	count++;
	
	if(params->aborted){
		printfColor(COLOR_BRIGHT|COLOR_RED, "Aborted root monitor operation: %s!\n", root->name);
	}else{
		assert(params->overlapped == &root->overlapped);

		if(params->result){
			if(params->bytesTransferred){
				FILE_NOTIFY_INFORMATION* fni;

				for(fni = (FILE_NOTIFY_INFORMATION*)root->changeBuffer;
					fni;
					fni = fni->NextEntryOffset ? (FILE_NOTIFY_INFORMATION*)((U8*)fni + fni->NextEntryOffset) : NULL)
				{
					char	fileName[MAX_PATH];
					S32		len = WideCharToMultiByte(CP_UTF8, 0, fni->FileName, fni->FileNameLength / sizeof(fni->FileName[0]), fileName, ARRAY_SIZE(fileName), NULL, NULL);
					S32		i;
					S32		first = 1;
					S32		found = 0;
					S32		clearLastRename = 0;
					
					fileName[len] = 0;
					
					forwardSlashes(fileName);
					
					//printf("notify: %s\n", fileName);
					
					for(i = 0; i < watchedFolders.count; i++){
						WatchedFolder* folder = watchedFolders.folder[i];

						if( folder->root == root &&
							!strnicmp(folder->name + 3, fileName, len) &&
							isNullOrSlash(folder->name[3 + len]))
						{
							found = 1;
							
							switch(fni->Action){
								xcase FILE_ACTION_ADDED:{
									printfColor(COLOR_BRIGHT|COLOR_GREEN,
												"%d. created:      \"%s%s\"\n",
												count,
												root->name,
												fileName);
									
									scanWatchedFolder(folder);
								}
								
								xcase FILE_ACTION_REMOVED:{
									printfColor(COLOR_BRIGHT|COLOR_RED,
												"%d. removed:      \"%s%s\"\n",
												count,
												root->name,
												fileName);

									scanWatchedFolder(folder);
								}
								
								xcase FILE_ACTION_RENAMED_OLD_NAME:{
									printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
												"%d. renamed from: \"%s%s\"\n",
												count,
												root->name,
												fileName);
												
									if(first){
										first = 0;
										
										strcpy(lastRename, fileName);
										
										lastRenameWatched = 1;
									}
									
									scanWatchedFolder(folder);
								}
								
								xcase FILE_ACTION_RENAMED_NEW_NAME:{
									if(	lastRename[0] &&
										!lastRenameWatched)
									{
										clearLastRename = 1;
										
										printfColor(COLOR_RED|COLOR_GREEN,
													"%d. renamed from: \"%s%s\" (not watched)\n",
													count,
													root->name,
													lastRename);
													
										consoleSetDefaultColor();
									}
									
									printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
												"%d. renamed to:   \"%s%s\"\n",
												count,
												root->name,
												fileName);

									scanWatchedFolder(folder);
								}
								
								xdefault:{
									printfColor(COLOR_RED,
												"%d. unhandled (%d): \"%s%s\"\n",
												count,
												fni->Action,
												root->name,
												fileName);
								}
							}
						}
						
						if(clearLastRename){
							lastRename[0] = 0;
						}
					}
					
					if(!found){
						if(fni->Action == FILE_ACTION_RENAMED_NEW_NAME){
							if(lastRename[0]){
								if(lastRenameWatched){
									consoleSetColor(COLOR_RED|COLOR_GREEN, 0);
									printf(	"%d. renamed to:   \"%s%s\" (not watched)\n",
											count,
											root->name,
											fileName);
									consoleSetDefaultColor();
								}
								
								lastRename[0] = 0;
							}
						}
						else if(fni->Action == FILE_ACTION_RENAMED_OLD_NAME){
							Strncpyt(lastRename, fileName);
							lastRenameWatched = 0;
						}
					}
				}
			}
		}else{
			printf("Buffer overflow: %s\n", root->name);
		}
	}
	
	startWatchingRoot(root);
}

static WatchedRoot* addWatchedRoot(const char* filePath){
	WatchedRoot* root;
	S32 i;
	
	// Make sure it starts with "[a-z]:/"
	
	if(	tolower(filePath[0]) < 'a' ||
		tolower(filePath[0]) > 'z' ||
		filePath[1] != ':' ||
		filePath[2] != '/')
	{
		FatalErrorf("Invalid folder root: %s\n", filePath);
		return NULL;
	}
	
	// Find out if the same watch already exists.
	
	for(i = 0; i < watchedRoots.count; i++){
		root = watchedRoots.root[i];
		
		if(!strnicmp(root->name, filePath, 3)){
			// Already exists.
			
			return root;
		}
	}
	
	root = *(WatchedRoot**)dynArrayAddp(	watchedRoots.root,
											watchedRoots.count,
											watchedRoots.maxCount,
											calloc(1, sizeof(*root)));
											
	// Copy the drive letter, colon, and slash: "c:/"

	strncpyt(root->name, filePath, 4);
	
	// Create the watch.
	
	root->handle = CreateFile_UTF8(	root->name,											// pointer to the file name
								FILE_LIST_DIRECTORY,								// access (read/write) mode
								FILE_SHARE_WRITE|FILE_SHARE_READ|FILE_SHARE_DELETE, // share mode
								NULL,												// security descriptor
								OPEN_EXISTING,										// how to create
								FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OVERLAPPED,	// file attributes
								NULL);												// file with attributes to copy

	if(root->handle == INVALID_HANDLE_VALUE){
		FatalErrorf("Can't get handle to root: %s\n", root->name);
	}
	
	// Start the IO completion port.
	
	root->dirMonCompletionHandle = dirMonCompletionPortAddObject(NULL, root->handle, root, handleRootNotification);
	
	root->changeBufferSize = 1024*1024;
	root->changeBuffer = malloc(root->changeBufferSize);
	
	startWatchingRoot(root);
	
	return root;
}

static WatchedFolder* addWatchedFolder(const char* dir){
	char			fixedName[MAX_PATH];
	WatchedFolder*	folder;
	S32				len1;
	S32				i;
	
	// Format the name properly (c:/game/data).

	Strncpyt(fixedName, dir);

	forwardSlashes(fixedName);

	while(strEndsWith(fixedName, "/")){
		fixedName[strlen(fixedName) - 1] = 0;
	}
	
	// Check for dupes.
	
	len1 = (S32)strlen(fixedName);

	for(i = 0; i < watchedFolders.count; i++){
		S32 len2;
		folder = watchedFolders.folder[i];
		len2 = (S32)strlen(folder->name);
		
		if(	!strnicmp(folder->name, fixedName, len1) &&
			isNullOrSlash(folder->name[len1])
			||
			!strnicmp(fixedName, folder->name, len2) &&
			isNullOrSlash(fixedName[len2]))
		{
			FatalErrorf("Duplicate folders: \"%s\" and \"%s\"!!!", fixedName, folder->name);
		}
	}
	
	// Create a new watched directory.

	folder = *(WatchedFolder**)dynArrayAddp(watchedFolders.folder,
											watchedFolders.count,
											watchedFolders.maxCount,
											calloc(1, sizeof(*folder)));

	folder->name = strdup(fixedName);
	folder->nameLen = (S32)strlen(folder->name);
	
	printf("Adding watched folder: %s\n", folder->name);

	// Add watched root.
	
	folder->root = addWatchedRoot(fixedName);

	return folder;
}

void skipUTFHeader(FILE *pFile)
{
	U8 buf[3] = {0};

	fread(buf, 3, 1, pFile);

	if (buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
	{
		return;
	}

	fseek(pFile, 0, SEEK_SET);
	
}

static void readWatchedFolderList(void){
	char	fileName[MAX_PATH];
	FILE*	f;
	int i;
	WatchedFolder** eaPreloads = NULL;

	fileGetCrypticSettingsFilePath(SAFESTR(fileName), "fileWatch.txt");
	forwardSlashes(fileName);

	f = fopen(fileName, "rt");
	
	skipUTFHeader(f);

	if(!f){
		printf("No \"fileWatch.txt\" found; using defaults!\n");

		addWatchedFolder("c:/Cryptic");
		
		addWatchedFolder("c:/Core/data");
		addWatchedFolder("c:/Core/tools");
		addWatchedFolder("c:/Core/src");

		addWatchedFolder("c:/Fightclub/data");
		addWatchedFolder("c:/Fightclub/tools");
		addWatchedFolder("c:/Fightclub/src");
		addWatchedFolder("c:/Fightclub/docs");
		addWatchedFolder("c:/Fightclub/CoreSrc");
		addWatchedFolder("c:/Fightclub/CoreData");

		addWatchedFolder("c:/FightclubFix/data");
		addWatchedFolder("c:/FightclubFix/tools");
		addWatchedFolder("c:/FightclubFix/src");
		addWatchedFolder("c:/FightclubFix/docs");
		addWatchedFolder("c:/FightclubFix/CoreSrc");
		addWatchedFolder("c:/FightclubFix/CoreData");

		addWatchedFolder("c:/StarTrek/data");
		addWatchedFolder("c:/StarTrek/tools");
		addWatchedFolder("c:/StarTrek/src");
		addWatchedFolder("c:/StarTrek/docs");
		addWatchedFolder("c:/StarTrek/CoreSrc");
		addWatchedFolder("c:/StarTrek/CoreData");

		addWatchedFolder("c:/StarTrekFix/data");
		addWatchedFolder("c:/StarTrekFix/tools");
		addWatchedFolder("c:/StarTrekFix/src");
		addWatchedFolder("c:/StarTrekFix/docs");
		addWatchedFolder("c:/StarTrekFix/CoreSrc");
		addWatchedFolder("c:/StarTrekFix/CoreData");
	}else{
		char buffer[100];
		
		printf("Reading from \"%s\":\n", fileName);

		while(fgets(buffer, sizeof(buffer), f))
		{
			
			removeLeadingAndFollowingSpaces(buffer);

			if(buffer[0] && strEndsWith(buffer, "\n")){
				buffer[strlen(buffer) - 1] = 0;
			}

			if(	!buffer[0]
				||
				buffer[0] == '#'
				||
				buffer[0] == ';'
				||
				buffer[0] == '/' &&
				buffer[1] == '/')
			{
				continue;
			}

			if (strnicmp(buffer,"Preload ",8) == 0) 
			{
				eaPush(&eaPreloads, addWatchedFolder(&buffer[8]));
			}
			else
			{
				addWatchedFolder(buffer);
			}
		}

		fclose(f);
	}

	if (eaSize(&eaPreloads))
	{
		// Scan the watched folders that are set to preload
		printf("Preloading requested folders\n");
		for(i=0; i<eaSize(&eaPreloads); ++i) {
			scanWatchedFolder(eaPreloads[i]);
		}
		eaDestroy(&eaPreloads);
	}
		
	printf(	"\nDone: %s folders, %s files\n\n",
			getCommaSeparatedInt(fileWatch.totalFolderCount),
			getCommaSeparatedInt(fileWatch.totalFileCount));
}

static void fcCallback(	FolderCache* fc,
						FolderNode* changedNode,
						int virtual_location,
						const char* relpath,
						S32 when,
						void *userData)
{
	//printf("%d: %s%s\n", when, fc->gamedatadirs[0], relpath);
	
	if(when & FOLDER_CACHE_CALLBACK_DELETE){
		S32 i;
		
		// Check if any current scans are pointing at this node, in which case
		// go to the next node and set it to not jump next time.
		
		for(i = 0; i < fileClients.count; i++){
			FileClient* client = fileClients.client[i];
			S32 j;
			
			for(j = 0; j < client->scans.count; j++){
				FolderScanProgress* scan = client->scans.scan[j];
				FolderNode* node;
				
				// Check all parents.
				
				for(node = scan->curNode; node; node = node->parent){
					if(node == changedNode){
						if(node == scan->curNode){
							// Just go to the next one, because the parent is still valid.
							
							scan->curNode = scan->curNode->next;
						}else{
							// A parent was removed so cancel this scan.
						
							scan->curNode = NULL;
						}
						break;
					}
				}
			}
		}
	}
}

static void printFolderNode(FolderNode* node, S32 isDeepest){
	if(!node){
		return;
	}

	printFolderNode(node->parent, 0);

	printf("%s%s", node->name, isDeepest && !node->is_dir ? "" : "/");

	if(isDeepest){
		printf("\n");
	}
}

static S32 fnOp(FolderNode* node){
	printFolderNode(node, 1);

	return 1;
}

static void printUsage(void){
	printf(	"\n"
			"   FileWatcher v%d (%s)\n"
			"-----------------------------------------------------------------------------------------------\n"
			"\n"
			"FileWatcher makes things faster, it is your friend and you love it.\n"
			"You can tell Martin or Jimb how much you love FileWatcher.\n"
			"You can also tell them if there is a problem with it.\n"
			"\n",
			FILEWATCHER_VERSION,
			getExecutableName());
	
	printfColor(COLOR_BRIGHT|COLOR_RED,
				"\n"
				"            ****  ****   ********   ****      ****    ***** \n"
				"            ****  ****   ********    ****    ****    *******  \n"
				"            ****  ****   ****         ****  ****     *******   \n"
				"            **********   ********      ********       *****    \n"
				"            **********   ********       ******         ***  \n"
				"            **********   ********        ****          ***  \n"
				"            ****  ****   ****            ****                   \n"
				"            ****  ****   ********        ****          ***      \n"
				"            ****  ****   ********        ****          ***      \n"
				"\n"
				"      ATTENTION: DO NOT CLOSE FILEWATCHER, OR EVERYTHING WILL BE SLOWER!\n"
				"                 SLOWNESS IS LAME AND WEAK!  BE NON-LAME AND NON-WEAK!\n"
				"\n\n");
}

static void printHelp(void){
	printf(	"\n"
			"Command Line Options:\n"
			"\n"
			" -simulate : Starts a simulator for running folder scans and stats.\n"
			"\n");
}

static S32 windowVisible = 1;
static UINT taskBarCreateMessage;

static HWND createWndIconHandler();

static void createShellIcon(S32 forceCreate){
	static HICON	hIcon;
	static S32		iconExists;
	static S32		prevWindowVisible;
	
	if(	forceCreate ||
		prevWindowVisible != windowVisible ||
		!iconExists)
	{
		NOTIFYICONDATA data = {0};
		S32 i;
		char *pTemp = NULL;
		
		for(i = 0; i < 2; i++){
			prevWindowVisible = windowVisible;
			
			if(!hIcon){
				hIcon = getIconColoredLetter('F', ICON_LETTER_COLOR);
			}
			
			data.cbSize = sizeof(data);
			data.hWnd = createWndIconHandler();
			data.uID = 0;
			data.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
			data.hIcon = hIcon;
			data.uCallbackMessage = WM_USER;
			
			estrPrintf(&pTemp,
					FORMAT_OK(windowVisible ?
						"FileWatcher v%d.\n\nClick here or minimize to HIDE the window." :
						"FileWatcher v%d.\n\nDouble-click here to SHOW the window."),
					FILEWATCHER_VERSION);

			UTF8_To_UTF16_Static(pTemp, data.szTip, ARRAY_SIZE(data.szTip));

			estrDestroy(&pTemp);

			iconExists = Shell_NotifyIcon(iconExists ? NIM_MODIFY : NIM_ADD, &data);
			
			if(iconExists){
				break;
			}
		}
	}
}

static void notifyStatus(const char *title, const char *msg, U32 timeout)
{
	NOTIFYICONDATA data = {0};
	createShellIcon(0);

	data.cbSize = sizeof(data);
	data.hWnd = createWndIconHandler();
	data.uID = 0;
	data.uFlags = NIF_INFO;
	data.dwInfoFlags = NIIF_INFO;
	data.uTimeout = 30;

	UTF8_To_UTF16_Static(msg, data.szInfo, ARRAY_SIZE(data.szInfo));
	UTF8_To_UTF16_Static(title, data.szInfoTitle, ARRAY_SIZE(data.szInfoTitle));

	Shell_NotifyIcon(NIM_MODIFY, &data);
}

static void showWindow(S32 show){
	HWND hwnd = compatibleGetConsoleWindow();
	
	show = show ? 1 : 0;

	ShowWindow(hwnd, show ? SW_SHOW : SW_HIDE);
	
	if(show){
		if(IsIconic(hwnd)){
			ShowWindow(hwnd, SW_RESTORE);
		}
		
		SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		
		SetForegroundWindow(hwnd);
	}
	
	if(show != windowVisible){
		windowVisible = show;
		
		createShellIcon(0);
	}
}

static void deleteTrayIcon(void){
	NOTIFYICONDATA data = {0};
	
	data.cbSize = sizeof(data);
	data.hWnd = createWndIconHandler();
	data.uID = 0;

	Shell_NotifyIcon(NIM_DELETE, &data);
}

static LRESULT CALLBACK iconWindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam){
	switch(Msg){
		xcase WM_USER:{
			static S32 eatDoubleClick;
			
			// The icon is being interacted with.
			
			if(	lParam == WM_LBUTTONDOWN ||
				lParam == WM_RBUTTONDOWN)
			{
				if(windowVisible){
					showWindow(0);
					eatDoubleClick = 1;
				}else{
					eatDoubleClick = 0;
				}
			}
			else if(lParam == WM_LBUTTONDBLCLK ||
					lParam == WM_RBUTTONDBLCLK)
			{
				if(	!eatDoubleClick &&
					!windowVisible)
				{
					showWindow(1);
				}
			}
		}
		
		xcase WM_CLOSE:{
			// Sent from another FileWatcher to kill this process.
			
			deleteTrayIcon();
			
			exit(0);
		}
		
		xdefault:{
			if(Msg == taskBarCreateMessage){
				createShellIcon(1);
			}
		}
	}
		
	return DefWindowProc(hWnd, Msg, wParam, lParam);
}

static HWND createWndIconHandler(void){
	static HWND hwndIconHandler;
						
	if(!hwndIconHandler){
		WNDCLASSEX winClass = {0};

		winClass.cbSize			= sizeof(winClass);
		winClass.style			= CS_OWNDC | CS_DBLCLKS;
		winClass.lpfnWndProc	= (WNDPROC)iconWindowProc;
		winClass.cbClsExtra		= 0;
		winClass.cbWndExtra		= 0;
		winClass.hInstance		= GetModuleHandle(NULL);
		winClass.hCursor		= LoadCursor( NULL, IDC_HAND );
		winClass.hbrBackground	= NULL;
		winClass.lpszMenuName	= L"";
		winClass.lpszClassName	= L"FileWatcherWindow";
		
		RegisterClassEx(&winClass);

		hwndIconHandler = CreateWindow_UTF8(	"FileWatcherWindow",
										"",
										WS_POPUP,
										0,
										0,
										300,
										300,
										NULL,
										NULL,
										GetModuleHandle(0),
										NULL);
										
		if(!hwndIconHandler){
			FatalErrorf("Can't create icon handler window: %d", GetLastError());
		}
		
		taskBarCreateMessage = RegisterWindowMessage(L"TaskbarCreated");
		
		createShellIcon(1);
		
		// SDANGELO: No longer show window automatically when Filewatcher is autostarted.
		//           We can do this because the auto-start is no longer a long-blocking action.
		//if(fileWatch.autoStart){
		//	showWindow(1);
		//}
	
		Sleep(100);
	}
	
	return hwndIconHandler;
}

static BOOL deleteIconAtExit(DWORD fdwCtrlType){
	switch(fdwCtrlType){
		xcase CTRL_CLOSE_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_C_EVENT:
		{
			deleteTrayIcon();
			
			return FALSE;
		}
	}
	
	return FALSE;
}

static DWORD WINAPI iconThread(void* unused){
	HWND hwnd = createWndIconHandler();
	
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)deleteIconAtExit, 1);
	
	while(1){
		MSG msg;
		S32 ret = GetMessage(&msg, hwnd, 0, 0);
		
		if(ret > 0){
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}

static HANDLE acquireFileWatcherVersionMutex(S32 pid, S32 version){
	char versionString[100] = "";

	if(version > 0){
		sprintf(versionString, ".%d", version);
	}
	
	return acquireMutexHandleByPID("FileWatcher.", pid, versionString);
}

static BOOL CALLBACK closeThisWindow(HWND hwnd, LPARAM lParam){
	char *pName = NULL;
	
	GetWindowText_UTF8(hwnd, &pName);
	
	printf("  Window 0x%8.8p: %s\n", hwnd, pName);
	
	PostMessage(hwnd, WM_CLOSE, 0, 0);
	
	estrDestroy(&pName);

	return TRUE;
}

static S32 closeWindowInThread(const ForEachThreadCallbackData* data){
	printf(	"Sending close to windows in process %d (%s), thread %d:\n",
			data->pid,
			(char*)data->userPointer,
			data->tid);
	
	EnumThreadWindows(data->tid, closeThisWindow, 0);
	
	Sleep(500);
	
	return 1;
}

static void sendCloseToProcess(const char* exeName, S32 pid, S32 useProcessKill){
	forEachThread(closeWindowInThread, pid, (void*)exeName);

	// And just in case it's still there...

	if(useProcessKill){
		HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
		
		if(hProcess){
			printfColor(COLOR_BRIGHT|COLOR_RED, "Killing FileWatcher process ID %d:", pid);
			
			TerminateProcess(hProcess, 0);
			
			WaitForSingleObject(hProcess, INFINITE);
			
			printfColor(COLOR_BRIGHT|COLOR_RED, "DONE!\n");
			
			CloseHandle(hProcess);
		}
	}
}

typedef struct KillFileWatchersData {
	S32 killCount;
	S32 highestVersion;
} KillFileWatchersData;

static S32 killOldFileWatchersHelper(const ForEachProcessCallbackData* data){
	KillFileWatchersData*	kfwd = data->userPointer;
	DWORD					processID = data->pid;
	S32						killThisProcess = 0;
	S32						hasMainMutex = 0;
	S32						version;
	HANDLE					hMutex;
	
	if(processID == GetCurrentProcessId()){
		return 1;
	}
	
	// Check if this process has the main mutex for it's PID.
	
	hMutex = acquireFileWatcherVersionMutex(processID, 0);
	
	if(!hMutex){
		hasMainMutex = 1;
	}else{
		ReleaseMutex(hMutex);
		CloseHandle(hMutex);
	}
	
	// Check if this process has the mutex for any current or previous FileWatcher version.
	
	for(version = FILEWATCHER_VERSION; version > 0; version--){
		hMutex = acquireFileWatcherVersionMutex(processID, version);

		if(hMutex){
			ReleaseMutex(hMutex);
			CloseHandle(hMutex);
		}else{
			// The process has the mutex, so a FileWatcher of this version is running.
			
			MAX1(kfwd->highestVersion, version);
			
			if(version != FILEWATCHER_VERSION){
				killThisProcess = 1;
			}
			
			break;
		}
	}
	
	if(	!killThisProcess &&
		version != FILEWATCHER_VERSION)
	{
		if(hasMainMutex){
			// Must be a newer version, so we'll return that.
			
			kfwd->highestVersion = FILEWATCHER_VERSION + 1;
		}else{
			// Didn't have the mutex, and isn't the latest version, so see if it's a
			// FileWatcher.exe process, in which case it's an old FileWatcher and must be killed.

			if(strEndsWith(data->exeFileName, "FileWatcher.exe")){
				killThisProcess = 1;
			}
		}
	}
	
	if(killThisProcess){
		kfwd->killCount++;
	
		sendCloseToProcess(data->exeFileName, processID, !hasMainMutex);
	}
	
	return 1;
}

static S32 killOldFileWatchers(void){
	KillFileWatchersData kfwd = {0};
	
	forEachProcess(killOldFileWatchersHelper, &kfwd);

	if(kfwd.killCount){
		// Sleep to allow the killed processes to clean up.
		
		Sleep(500);
	}
	
	return kfwd.highestVersion;
}

static S32 acquireMutex(HANDLE hMutex, S32 wait){
	DWORD hr;
	WaitForSingleObjectWithReturn(hMutex, wait, hr);
	switch(hr){
		xcase WAIT_OBJECT_0:
		case WAIT_ABANDONED:{
			return 1;
		}
	}
	
	return 0;
}

static void acquireFileWatcherMainMutex(void){
	HANDLE	hMutexCheck = CreateMutex_UTF8(NULL, FALSE, fileWatchGetCheckRunningMutexName());
	HANDLE	hMutexRunning;
	S32		retVal = 1;
	
	if(!hMutexCheck){
		FatalErrorf("Another user's process has the FileWatcher mutex: %s", fileWatchGetCheckRunningMutexName());
	}else{
		if(!acquireMutex(hMutexCheck, INFINITE)){
			assert(0);
		}
		
		hMutexRunning = CreateMutex_UTF8(NULL, FALSE, fileWatchGetRunningMutexName());
		
		if(!hMutexRunning){
			FatalErrorf("Another user's process has the FileWatcher mutex: %s", fileWatchGetRunningMutexName());
			
			retVal = 0;
		}else{
			if(!acquireMutex(hMutexRunning, 500)){
				FatalErrorf("Another FileWatcher.exe is still running, but it shouldn't be.  Kill one of them.");
			}
		}
		
		ReleaseMutex(hMutexCheck);
		CloseHandle(hMutexCheck);
	}
}

static void checkForMultipleFileWatchers(void){
	static HANDLE	hMutex;
	HANDLE			hMutexCheckRunning = CreateMutex_UTF8(NULL, FALSE, fileWatchGetCheckRunningMutexName());
	S32				highestVersion;
	DWORD hr;

	if(hMutex){
		return;
	}
	
	// Make sure we're the only process doing this right now.
	
	if(!hMutexCheckRunning){
		return;
	}
	
	WaitForSingleObjectWithReturn(hMutexCheckRunning, INFINITE, hr)
	switch(hr){
		xcase WAIT_OBJECT_0:
		case WAIT_ABANDONED:{
		}
		
		xdefault:{
			assert(0);
		}
	}
	
	// Kill old processes and get the highest running version number.

	highestVersion = killOldFileWatchers();

	if(highestVersion >= FILEWATCHER_VERSION){
		// A current version is already running, so exit.
		
		printfColor(COLOR_BRIGHT|COLOR_GREEN,
					"A current or higher version of FileWatcher is already running.\n\nExiting...");
		
		if(highestVersion > FILEWATCHER_VERSION){
			Sleep(2000);
		}else{
			Sleep(500);
		}
		
		exit(0);
	}

	acquireFileWatcherVersionMutex(-1, 0);
	acquireFileWatcherVersionMutex(-1, FILEWATCHER_VERSION);
	
	acquireFileWatcherMainMutex();

	ReleaseMutex(hMutexCheckRunning);
	CloseHandle(hMutexCheckRunning);
}

static void statCheck(void){
	#if 0
	while(1){
		const char*			nameDec = "c:\\documents and settings\\msimpson\\desktop\\blah\\dec\\blah.txt";
		const char*			nameJun = "c:\\documents and settings\\msimpson\\desktop\\blah\\jun\\blah.txt";
		struct _stat		statDec;
		struct _stat		statJun;
		WIN32_FIND_DATA 	wfdDec;
		WIN32_FIND_DATA 	wfdJun;
		__time32_t				curTime = time(NULL);
		__time32_t				timeDec;
		__time32_t				timeJun;
		struct _finddata_t	fdDec;
		struct _finddata_t	fdJun;
		
		_stat(nameDec, &statDec);
		_stat(nameJun, &statJun);
		
		FindClose(FindFirstFile(nameDec, &wfdDec));
		FindClose(FindFirstFile(nameJun, &wfdJun));
		
		_FileTimeToUnixTime(&wfdDec.ftLastWriteTime, &timeDec, FALSE);
		_FileTimeToUnixTime(&wfdJun.ftLastWriteTime, &timeJun, FALSE);
		
		_findfirst(nameDec, &fdDec);
		_findfirst(nameJun, &fdJun);
		
		#define print(name, timeIn) {__time32_t x = timeIn;printf(name"%-20d%3s %s", x, localtime(&x)->tm_isdst ? "DST" : "", asctime(gmtime(&x)));}
		print("cur:      ", curTime);
		print("dec stat: ", statDec.st_mtime);
		print("dec adj:  ", statTimeToUTC(statDec.st_mtime));
		print("dec ff:   ", fdDec.time_write);
		print("dec ffadj:", statTimeToUTC(fdDec.time_write));
		print("dec fff:  ", timeDec);
		print("jun stat: ", statJun.st_mtime);
		print("jun adj:  ", statTimeToUTC(statJun.st_mtime));
		print("jun ff:   ", fdJun.time_write);
		print("jun ffadj:", statTimeToUTC(fdJun.time_write));
		print("jun fff:  ", timeJun);
		#undef print
		printf("-------------------------------------\n\n");
				
		if(_getch() == 27){
			exit(0);
		}
	}
	#endif
}

U32 fwGetNoAutoStart(S32 update, U32 value){
	RegReader	rr = createRegReader();
	U32			noAutoStart;
	
	initRegReader(rr, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\FileWatcher");
	
	if(!rrReadInt(rr, "NoAutoStart", &noAutoStart)){
		noAutoStart = 0;
	}
	
	if(update){
		rrWriteInt(rr, "NoAutoStart", value ? 1 : 0);
	}
	
	destroyRegReader(rr);
	
	return noAutoStart;
}

static void processCommandLine(S32 argc, char** argv){
	if(argc >= 2){
		if(	!stricmp(argv[1], "-simulate") ||
			!stricmp(argv[1], "-simulate2"))
		{
			simulateClient(!stricmp(argv[1], "-simulate2"));
		}
		else if(!stricmp(argv[1], "-autostart")){
			fileWatch.autoStart = 1;
			
			if(fwGetNoAutoStart(0, 0)){
				exit(0);
			}
		}
	}	
}

static void checkKeyboardInput(void){
	S32 key;
	
	if(!_kbhit()){
		return;
	}
	
	key = tolower(_getch());

	switch(key){
		xcase 224:{
			_getch();
		}
		
		xcase 27:{
			printf(	"\n"
					"-----------------------------------------------------\n"
					"  FileWatcher v%d: %s folders, %s files\n",
					FILEWATCHER_VERSION,
					getCommaSeparatedInt(fileWatch.totalFolderCount),
					getCommaSeparatedInt(fileWatch.totalFileCount));
					
			printf(	"-----------------------------------------------------\n"
					"  ESC  : This menu.\n"
					"    A  : Toggle FileWatcher auto-start from gimme (currently %s).\n"
					"    C  : Print current client list.\n"
					"    F  : Print stats that are not found in watched folders.\n"
					"    M  : Print memory usage.\n"
					"    P  : Toggle printing creates and deletes.\n"
					"    R  : Rescan all watched folders.\n"
					"    S  : Start a simulation client.\n"
					"    T  : Toggle title bar stats.\n"
					"    U  : Print stats that are in unwatched folders.\n"
					"    W  : Goto the FileWatcher wiki page: "
					"    ?  : See help.\n"
					"-----------------------------------------------------\n"
					"\n",
					fwGetNoAutoStart(0, 0) ? "OFF" : "ON");
		}
		
		xcase 'a':{
			S32 noAutoStart = !fwGetNoAutoStart(0, 0);
			
			fwGetNoAutoStart(1, noAutoStart);
			
			printf("Auto-start: %s\n", noAutoStart ? "DISABLED" : "ENABLED");
		}
		
		xcase 'c':{
			S32 i;
			S32 count = 0;
			
			EnterCriticalSection(&fileWatch.csClientList);

			for(i = 0; i < fileClients.count; i++){
				FileClient* client = fileClients.client[i];
				
				if(client->connected){
					if(!count++){
						printf("Client list:\n");
					}

					printf(	"Pipe %d, handle %p, %d scans: App: %d:%s\n",
							i,
							client->hPipe,
							client->scans.count,
							client->processID,
							client->processName);
				}
			}
			
			LeaveCriticalSection(&fileWatch.csClientList);

			if(!count){
				printf("No clients connected.\n");
			}
			
			if(count != fileClients.connectedCount){
				printfColor(COLOR_BRIGHT|COLOR_RED,
							"Mismatched connected count (%d != %d).\n",
							count,
							fileClients.connectedCount);
			}
		}

		xcase 'f':{
			S32 on = fileWatch.printStatUnfound = !fileWatch.printStatUnfound;
			
			printf("Print stat unfound: %s\n", on ? "ON" : "OFF");
		}
		
		xcase 'm':{
			memMonitorDisplayStats();
		}
		
		xcase 'p':{
			S32 on = fileWatch.printCreatesAndDeletes = !fileWatch.printCreatesAndDeletes;
			
			printf("Print creates and deletes: %s\n", on ? "ON" : "OFF");
		}
		
		xcase 'r':{
			fileWatch.doRescan = 1;
			dirMonPostCompletion(NULL, fileWatch.inputThreadCompletionHandle);
		}
		
		xcase 's':{
			char buffer[1000];
			sprintf(buffer, "%s -simulate2", getExecutableName());
			system_detach(buffer, 0, 0);
		}

		xcase 't':{
			S32 on = fileWatch.showTitleBarStats = !fileWatch.showTitleBarStats;
			
			printf("Title bar stats: %s\n", on ? "ON" : "OFF");
		}
		
		xcase 'u':{
			S32 on = fileWatch.printStatUnwatched = !fileWatch.printStatUnwatched;
			
			printf("Print unwatched stats: %s\n", on ? "ON" : "OFF");
		}
		
		xcase 'w':{
			printf("Going to wiki page...\n");
			
			ShellExecute_UTF8(	NULL,
							"open",
							NULL,
							NULL,
							NULL,
							SW_NORMAL);
		}
		
		xcase '?':{
			printHelp();
		}
		
		xdefault:{
			printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN, "Unhandled key: %d\n", key);
		}
	}
}

static DWORD WINAPI inputThread(void* unused){
	while(1){
		autoTimerThreadFrameBegin(__FUNCTION__);
		
		if(windowVisible){
			char buffer[1000];
			
			if(	!IsWindowVisible(compatibleGetConsoleWindow()) ||
				IsIconic(compatibleGetConsoleWindow()))
			{
				showWindow(0);
			}

			if(!fileWatch.changingNodes){
				if(fileWatch.showTitleBarStats){
					sprintf(buffer,
							"FileWatcher v%d: %s folders, %s files, %d clients, %s messages, %s stats (%s unwatched, %s unfound)",
							FILEWATCHER_VERSION,
							getCommaSeparatedInt(fileWatch.totalFolderCount),
							getCommaSeparatedInt(fileWatch.totalFileCount),
							fileClients.connectedCount,
							getCommaSeparatedInt(fileClients.handledMessageCount),
							getCommaSeparatedInt(fileClients.statCount),
							getCommaSeparatedInt(fileClients.unwatchedStatCount),
							getCommaSeparatedInt(fileClients.unfoundStatCount));
				}else{
					strcpy(buffer, "FileWatcher");
				}
						
				setConsoleTitle(buffer);
			}

			checkKeyboardInput();
			
			Sleep(100);
		}else{
			Sleep(1000);
			
			if(IsWindowVisible(compatibleGetConsoleWindow())){
				showWindow(1);
			}
		}
		
		autoTimerThreadFrameEnd();
	}
}

static void checkForNeededRescans(void){
	PERFINFO_AUTO_START_FUNC();

	FOR_BEGIN(i, watchedFolders.count);
		WatchedFolder* folder = watchedFolders.folder[i];
		
		if(	folder->fc &&
			(	folder->fc->needsRescan ||
				fileWatch.doRescan))
		{
			folder->fc->needsRescan = 0;
			//printfColor(COLOR_RED|COLOR_BRIGHT, "Do a rescan!!!!!!\n");
			scanWatchedFolder(folder);
		}
	FOR_END;

	fileWatch.doRescan = 0;
	
	PERFINFO_AUTO_STOP();
}

static void inputThreadCompletionCallback(DirMonCompletionPortCallbackParams* params){
}

//#define JOURNAL_THING 1


int wmain(int argc, WCHAR** argv_wide)
{
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	gimmeDLLDisable(1);
	DO_AUTO_RUNS;

	autoTimerInit();
	
	#if JOURNAL_THING
	{
		void journalThing(void);
		
		journalThing();
		
		_getch();
	
		return;
	}
	#endif

	pigDisablePiggs(1);
	fileAllPathsAbsolute(true);
	fileDisableAutoDataDir();

	memMonitorInit();

	setConsoleTitle("FileWatcher Starting...");
	
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'F', ICON_LETTER_COLOR);
	
	statCheck();	
	
	FolderCacheSetMode(FOLDER_CACHE_MODE_DEVELOPMENT);
	
	consoleUpSize(120, 9999);

	printUsage();
	
	processCommandLine(argc, argv);

	InitializeCriticalSection(&fileWatch.csClientList);
	
	checkForMultipleFileWatchers();
	
	_beginthreadex(0, 0, iconThread, 0, 0, 0);

	// Set this process to not ask itself for file information.

	fileWatchSetDisabled(1);
	
	createNewClient();

	readWatchedFolderList();

	FolderCacheSetCallbackEx(FOLDER_CACHE_CALLBACK_ALL, "*", fcCallback, NULL);
	
	assert(_CrtCheckMemory());

	printfColor(COLOR_BRIGHT|COLOR_GREEN, "Done loading.  FileWatcher is now ACTIVE.\n");
	
	printf("-----------------------------------------------------------------------------------------------\n");

	printf("\nPress ESCAPE to print the keybind list.\n\n");

	showWindow(0);
			
	fileWatch.inputThreadCompletionHandle = dirMonCompletionPortAddObject(	NULL,
																			NULL,
																			NULL,
																			inputThreadCompletionCallback);
	
	_beginthreadex(0, 0, inputThread, 0, 0, 0);

	FolderCacheSetDirMonTimeout(INFINITE);
	FolderCacheSetNoDisableOnBufferOverruns(1);
	
	SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
	
	while(1){
		autoTimerThreadFrameBegin("main");
		
		do {
			EnterCriticalSection(&fileWatch.csClientList);
			
			// Create a new client pipe connection if all of the current ones are connected.
			
			createNewClient();

			// Start connecting on any pipes that have disconnected.
			
			connectDisconnectedPipes();
			
			LeaveCriticalSection(&fileWatch.csClientList);
		} while(fileClients.hasDisconnectedClient);
		
		// Do callbacks.  This will also handle io completion for client pipes.

		FolderCacheDoCallbacks();

		checkForNeededRescans();
		
		autoTimerThreadFrameEnd();
	}

	EXCEPTION_HANDLER_END
}

#if JOURNAL_THING
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>

static void journalThing(void)
{
	HANDLE hVol;
	CHAR Buffer[0x1000];

	USN_JOURNAL_DATA JournalData;
	READ_USN_JOURNAL_DATA ReadData = {0, 0xFFFFFFFF, FALSE, 0, 0};
	PUSN_RECORD UsnRecord;  

	DWORD dwBytes;
	DWORD dwRetBytes;
	int I;
	S32 done = 0;
	S32 totalUsnCount = 0;
	S32 totalUsnSize = 0;
	
	consoleUpSize(110, 9999);

	hVol = CreateFile(	"\\\\.\\c:", 
						GENERIC_READ | GENERIC_WRITE, 
						FILE_SHARE_READ | FILE_SHARE_WRITE,
						NULL,
						OPEN_EXISTING,
						0,
						NULL);

	if( hVol == INVALID_HANDLE_VALUE )
	{
		printf("CreateFile failed (%d)\n", GetLastError());
		return;
	}

	if( !DeviceIoControl( hVol, 
		FSCTL_QUERY_USN_JOURNAL, 
		NULL,
		0,
		&JournalData,
		sizeof(JournalData),
		&dwBytes,
		NULL) )
	{
		CREATE_USN_JOURNAL_DATA cd;
		
		cd.MaximumSize = 128 * (1 << 20);
		cd.AllocationDelta = (1 << 20);
		
		printf( "Query journal failed (%d)\n", GetLastError());

		if( !DeviceIoControl(	hVol, 
								FSCTL_CREATE_USN_JOURNAL, 
								&cd,
								sizeof(cd),
								NULL,
								0,
								&dwBytes,
								NULL))
		{
			printf("Can't create change journal! (%d)\n", GetLastError());
			return;
		}
	}

	ReadData.UsnJournalID = JournalData.UsnJournalID;

	printf( "Journal ID: %"FORM_LL"x\n", JournalData.UsnJournalID );
	printf( "FirstUsn: %"FORM_LL"x\n\n", JournalData.FirstUsn );

	for(I=0; !done; I++)
	{
		ZeroArray(Buffer);

		if( !DeviceIoControl(	hVol, 
								FSCTL_READ_USN_JOURNAL, 
								&ReadData,
								sizeof(ReadData),
								&Buffer,
								sizeof(Buffer),
								&dwBytes,
								NULL))
		{
			printf("Read journal failed (%d)\n", GetLastError());
			return;
		}
		
		totalUsnSize += dwBytes;

		dwRetBytes = dwBytes - sizeof(USN);
		
		if(!dwRetBytes){
			break;
		}

		// Find the first record
		UsnRecord = (PUSN_RECORD)(((PUCHAR)Buffer) + sizeof(USN));  

		//printf( "****************************************\n");

		while( dwRetBytes > 0 )
		{
			totalUsnCount++;
			
			if(0 || totalUsnCount % 123 == 0){
				FILETIME lt;
				SYSTEMTIME st;
				char buffer[1000];
				
				FileTimeToLocalFileTime((FILETIME*)&UsnRecord->TimeStamp, &lt);
				FileTimeToSystemTime(&lt, &st);
				
				GetTimeFormat(LOCALE_SYSTEM_DEFAULT, LOCALE_NOUSEROVERRIDE, &st, NULL, buffer, sizeof(buffer));
				
				printf("%"FORM_LL"x, %4.4d-%2.2d-%2.2d, %12s: ", UsnRecord->Usn, st.wYear, st.wMonth, st.wDay, buffer);
				
				printf(	"File name: %.*S\n", 
						UsnRecord->FileNameLength/2, 
						UsnRecord->FileName);
			}
			
			if(0){
				printf( "USN: (%d,%d,%d) %"FORM_LL"x:%"FORM_LL"x:%"FORM_LL"x    %d bytes\n",
						sizeof(UsnRecord->ParentFileReferenceNumber),
						sizeof(UsnRecord->FileReferenceNumber),
						sizeof(UsnRecord->Usn),
						UsnRecord->ParentFileReferenceNumber,
						UsnRecord->FileReferenceNumber,
						UsnRecord->Usn,
						UsnRecord->RecordLength);
						
				//UsnRecord->FileReferenceNumber
						
				printf(	"File name: %.*S\n", 
						UsnRecord->FileNameLength/2, 
						UsnRecord->FileName);
				printf( "Reason: 0x%x\n", UsnRecord->Reason );
				printf( "\n" );
			}
			
			dwRetBytes -= UsnRecord->RecordLength;
			
			// Find the next record
			UsnRecord = (PUSN_RECORD)(((PCHAR)UsnRecord) + UsnRecord->RecordLength); 
		}
		// Update starting USN for next call
		ReadData.StartUsn = *(USN *)&Buffer; 
	}
	
	printf("\n\nTotal count: %d (%d bytes)\n", totalUsnCount, totalUsnSize);
}
#endif
