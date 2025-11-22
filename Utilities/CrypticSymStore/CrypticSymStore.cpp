#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <direct.h>
#include <Windows.h>
#include <assert.h>
#include <windows.h>
#include <winnt.h>
#include <fcntl.h>
#include <share.h>
#include <stdlib.h>
#include <io.h>
#include <vector>
#include <process.h>
#include "DbgHelp.h"
using namespace std;

#pragma comment(lib, "dbghelp.lib")
#pragma comment (lib, "ws2_32.lib")
#pragma comment (lib, "winmm.lib")

#define COPY_TIMEOUT 20000
#define TIMEOUT_MERGING 30000

vector<char*> foldersToRemove;

#define SYMBOL_ERROR_LOG_FILENAME "symerror.log"

const char *backup_store = "\\\\CASTOR\\symservfallback\\";

static bool dirExists(const char *dir)
{
	struct _stat32 file_status;
	char path[MAX_PATH];
	strcpy_s(path, dir);
	if (path[strlen(path)-1] == '\\')
		path[strlen(path)-1] = '\0';
	if (0==_stat32(path, &file_status))
		if (file_status.st_mode & _S_IFDIR)
			return true;
	return false;
}

static bool fileExists(const char *path)
{
	struct _stat32 file_status;
	if (0==_stat32(path, &file_status))
		if (!(file_status.st_mode & _S_IFDIR))
			return true;
	return false;
}

static size_t fileSize(const char *path)
{
	struct _stat32 file_status;
	if (0==_stat32(path, &file_status))
		return file_status.st_size;
	return -1;
}

static int strEndsWith(const char* str, const char* ending)
{
	int strLength;
	int endingLength;
	if(!str || !ending)
		return 0;

	strLength = (int)strlen(str);
	endingLength = (int)strlen(ending);

	if(endingLength > strLength)
		return 0;

	if(_stricmp(str + strLength - endingLength, ending) == 0)
		return 1;
	else
		return 0;
}

static int strStartsWith(const char* str, const char* start)
{
	if(!str || !start)
		return 0;

	return _strnicmp(str, start, strlen(start))==0;
}

const char *g_filename = "[unknown]";

void log(const char *fn, const char *buf)
{
	FILE *f = NULL;
	if (0 == fopen_s(&f, fn, "a+")) {
		char tbuf[1024];
		time_t t = time(NULL);
		ctime_s(tbuf, 1024, &t);
		tbuf[strlen(tbuf)-1] = '\0';
		fprintf(f, "%s: %s: %s", tbuf, g_filename, buf);
		fclose(f);
	}
}
#undef logf
void logf(const char *fn, const char *fmt, ...)
{
	va_list va;
	char buf[4096]={0};

	va_start(va, fmt);
	vsprintf_s(buf, fmt, va);
	va_end(va);
	if (!strEndsWith(buf, "\n"))
		strcat_s(buf, "\n");

	log(fn, buf);
}


void errorf(const char *fmt, ...)
{
	va_list va;
	char buf[4096]={0};

	va_start(va, fmt);
	vsprintf_s(buf, fmt, va);
	va_end(va);
	if (!strEndsWith(buf, "\n"))
		strcat_s(buf, "\n");
	printf("%s", buf);

	MakeSureDirectoryPathExists("C:\\temp\\");
	log("C:\\temp\\CrypticSymStore.log", buf);
}

DWORD AbsoluteSeek(HANDLE, DWORD);
VOID  ReadBytes(HANDLE, LPVOID, DWORD);
VOID  WriteBytes(HANDLE, LPVOID, DWORD);
VOID  CopySection(HANDLE, HANDLE, DWORD);

#define XFER_BUFFER_SIZE 2048

// Apparently previous versions of Visual Studio, or the WinSDK, or something, provided IMAGE_SIZEOF_NT_OPTIONAL_HEADER, but they no longer do, as of VS2010SP1, WinSDK v7.1.
#ifndef IMAGE_SIZEOF_NT_OPTIONAL_HEADER
#define IMAGE_SIZEOF_NT_OPTIONAL32_HEADER	224
#define IMAGE_SIZEOF_NT_OPTIONAL64_HEADER	240
#ifdef _WIN64
#define IMAGE_SIZEOF_NT_OPTIONAL_HEADER		IMAGE_SIZEOF_NT_OPTIONAL64_HEADER
#else
#define IMAGE_SIZEOF_NT_OPTIONAL_HEADER		IMAGE_SIZEOF_NT_OPTIONAL32_HEADER
#endif
#endif  // IMAGE_SIZEOF_NT_OPTIONAL_HEADER

bool getEXEInfo(const char *path, DWORD *timestamp, DWORD *sizeOfImage)
{
	HANDLE hImage;

	DWORD  SectionOffset;
	DWORD  CoffHeaderOffset;
	DWORD  MoreDosHeader[16];

	ULONG  ntSignature;

	IMAGE_DOS_HEADER      image_dos_header;
	IMAGE_FILE_HEADER     image_file_header;
	IMAGE_OPTIONAL_HEADER image_optional_header;

	/*
	*  Open the reference file.
	*/ 
	hImage = CreateFile(path,
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	if (INVALID_HANDLE_VALUE == hImage)
	{
		errorf("Could not open %s, error %lu\n", path, GetLastError());
		return false;
	}

	/*
	*  Read the MS-DOS image header.
	*/ 
	ReadBytes(hImage,
		&image_dos_header,
		sizeof(IMAGE_DOS_HEADER));

	if (IMAGE_DOS_SIGNATURE != image_dos_header.e_magic)
	{
		errorf("Sorry, I do not understand this file.\n");
		CloseHandle(hImage);
		return false;
	}

	/*
	*  Read more MS-DOS header.       */ 
	ReadBytes(hImage,
		MoreDosHeader,
		sizeof(MoreDosHeader));

	/*
	*  Get actual COFF header.
	*/ 
	CoffHeaderOffset = AbsoluteSeek(hImage, image_dos_header.e_lfanew) +
		sizeof(ULONG);

	ReadBytes (hImage, &ntSignature, sizeof(ULONG));

	if (IMAGE_NT_SIGNATURE != ntSignature)
	{
		errorf("Missing NT signature. Unknown file type.\n");
		CloseHandle(hImage);
		return false;
	}

	SectionOffset = CoffHeaderOffset + IMAGE_SIZEOF_FILE_HEADER +
		IMAGE_SIZEOF_NT_OPTIONAL_HEADER;

	ReadBytes(hImage,
		&image_file_header,
		IMAGE_SIZEOF_FILE_HEADER);

	/*
	*  Read optional header.
	*/ 
	ReadBytes(hImage,
		&image_optional_header,
		IMAGE_SIZEOF_NT_OPTIONAL_HEADER);

	*timestamp = image_file_header.TimeDateStamp;
	*sizeOfImage = image_optional_header.SizeOfImage;
	CloseHandle(hImage);
	return true;
}

DWORD
AbsoluteSeek(HANDLE hFile,
			 DWORD  offset)
{
	DWORD newOffset;

	if ((newOffset = SetFilePointer(hFile,
		offset,
		NULL,
		FILE_BEGIN)) == 0xFFFFFFFF)
	{
		errorf("SetFilePointer failed, error %lu.\n", GetLastError());
		exit(-1);
	}

	return newOffset;
}

VOID
ReadBytes(HANDLE hFile,
		  LPVOID buffer,
		  DWORD  size)
{
	DWORD bytes;

	if (!ReadFile(hFile,
		buffer,
		size,
		&bytes,
		NULL))
	{
		errorf("ReadFile failed, error %lu.\n", GetLastError());
		exit(-1);
	}
	else if (size != bytes)
	{
		errorf("Read the wrong number of bytes, expected %lu, got %lu.\n",
			size, bytes);
		exit(-1);
	}
}

void backSlashes(char *s)
{
	for (char *c=s; *c; c++)
		if (*c=='/')
			*c='\\';
}




void gimmeEnsureDeleteLock(const char *fn) {
	while (-1==remove(fn)) {
		if (!fileExists(fn)) {
			//printf("ERROR!  Lockfile deleted by someone other than you!\n"); // This is expected, as anyone waiting is constantly trying to delete the file
			break;
		}
		Sleep(1);
	}
}

const char *getUserName()
{
	static int gotUserName = 0;
	static char	name[1000];
	DWORD	name_len = sizeof(name);

	if(!gotUserName)
	{
		if(!GetUserName(name,&name_len))
			name[0] = '\0';
		gotUserName = 1;
	}
	return name;
}

const char *getComputerName()
{
	static int gotComputerName = 0;
	static char	name[1000];
	DWORD	name_len = sizeof(name);

	if(!gotComputerName)
	{
		if(!GetComputerName(name,&name_len))
			name[0] = '\0';
		gotComputerName = 1;
	}
	return name;
}

char *getLocalHostNameAndIPs(void)
{
	char hostname[80];
	static char ret[1024];
	static bool cached=false;
	struct hostent *phe;
	int i;
	WSADATA wsaData;

	if (cached) return ret;

	if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) {
		return NULL;
	}

	if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
		errorf("Error %d when getting local host name.\n", WSAGetLastError());
		strcpy_s(ret, "Error getting hostname");
		cached=true;
		return ret;
	}

	phe = gethostbyname(hostname);
	if (phe == 0) {
		errorf("Error: Bad host lookup.\n");
		strcpy_s(ret, "Error: bad host lookup");
		cached=true;
		return ret;
	}

	sprintf_s(ret, "%s", hostname);
	for (i = 0; phe->h_addr_list[i] != 0; ++i) {
		struct in_addr addr;
		memcpy(&addr, phe->h_addr_list[i], sizeof(struct in_addr));
		strcat_s(ret, " ");
		strcat_s(ret, inet_ntoa(addr));
	}

	WSACleanup();
	cached=true;
	return ret;
}

void gimmeWriteLockData(int lockfile_handle) {
	_write(lockfile_handle, getUserName(), (unsigned int)strlen(getUserName()));
	_write(lockfile_handle, "\r\n", (unsigned int)strlen("\r\n"));
	_write(lockfile_handle, getLocalHostNameAndIPs(), (unsigned int)strlen(getLocalHostNameAndIPs()));
}

int gimmeAcquireLock(const char *lockfilename) {
	int handle;
	MakeSureDirectoryPathExists((char*)lockfilename);
	_sopen_s(&handle, lockfilename, _O_CREAT | _O_EXCL | _O_WRONLY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
	if (handle>=0) {
		gimmeWriteLockData(handle);
	} else {
		_unlink(lockfilename);
	}
	return handle;
}

int gimmeUnaquireAndDeleteLock(int lockfile_handle, char *lockfile_name)
{
	if (lockfile_handle && 0==_close(lockfile_handle)) { // we had it open
		lockfile_handle=0;
	} else {
		// no handle, or error closing the file, just delete the lockfile
	}
	if (lockfile_name!=NULL) {
		gimmeEnsureDeleteLock(lockfile_name);
		lockfile_name=0;
	}
	return 0;
}

char *fileAlloc(const char *lockfilename, int *count)
{
	char *ret;
	FILE *f = NULL;
	if (0 == fopen_s(&f, lockfilename, "rb"))
	{
		*count = (int)fileSize(lockfilename);
		ret = (char*)calloc(*count + 1, 1);
		fread(ret, 1, *count, f);
		fclose(f);
	} else {
		ret = (char*)calloc(1, 1);
		*count = 0;
	}
	return ret;
}


#define MAX_WAITS 300
#define WAIT_TIME 100

int gimmeWaitToAcquireLock(char *lockfilename) {
	int lockfile_handle;
	int loopcount=MAX_WAITS-5000/WAIT_TIME; // wait only 5 seconds the first time

	getLocalHostNameAndIPs(); // Call this once first to cache the results, so that we are not needlessly locking the database while looking up our hostname!

	while ((lockfile_handle = gimmeAcquireLock(lockfilename)) < 0) {
		if (loopcount++ > MAX_WAITS) { 
			// We seem to have timed out, try and find out who has this locked
			int count;
			char *mem = fileAlloc(lockfilename, &count);
			printf("\nStill waiting to acquire database lock (%s); Currently locked by\n", lockfilename);
			if (mem==NULL || count==0 || strlen(mem)==0) {
				// error reading the file... perhaps someone else is just writing to it now...
				printf("UNKNOWN\n");
			} else {
				printf("%s\n", mem);
			}
			if (mem!=NULL) {
				free(mem);
				mem=NULL;
			}
			loopcount=0;
		} 
		Sleep(WAIT_TIME); 
	}
	return lockfile_handle;
}

HANDLE hProcess;
char storepath[MAX_PATH];
char *action;
bool g_remove_source = false;
bool g_did_a_file = false;
bool g_safe = true;
bool g_server_down = false; // If in g_safe mode, is the server down, should we not even try?
bool g_doing_merge = false;
int storeFile(char *filename, char *filenameforext);

int storeFileRecurse(const char *dir)
{
	WIN32_FIND_DATA wfd;
	HANDLE handle;
	char buf[1200];
	int ret = 0;
	BOOL good=TRUE;

	strcpy_s(buf,dir);
	strcat_s(buf,"/*");

	for(handle = FindFirstFile(buf, &wfd); good; good = FindNextFile(handle, &wfd))
	{
		if( wfd.cFileName[0] == '.')
		{
			continue;
		}
		if (wfd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
			continue;

		sprintf_s(buf, "%s%s%s", dir, strEndsWith(dir, "\\")?"":"\\", wfd.cFileName);

		char shortname[MAX_PATH];
		strcpy_s(shortname, buf);
		if (strEndsWith(shortname, ".txt") && strstr(shortname, "_v#"))
		{
			// Old gimme file
			char *s = strstr(shortname, "_v#");
			*s = '\0';
		}

		if (!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && (
			strEndsWith(shortname, ".pdb") ||
			strEndsWith(shortname, ".exe") ||
			strEndsWith(shortname, ".dll") ||
			strEndsWith(shortname, ".dle") ||
			strEndsWith(shortname, ".dli") ||
			strEndsWith(shortname, ".xex") ||
			strEndsWith(shortname, ".intermediate")))
		{
			int r = storeFile(buf, shortname);
			if (r)
				ret = r;
		}

		if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			int r = storeFileRecurse(buf);
			if (r)
				ret = r;
		}
	}
	FindClose(handle);
	return ret;
}

int fileCompare(const char *file1, const char *file2)
{
	size_t s1 = fileSize(file1);
	size_t s2 = fileSize(file2);
	if (s1 != s2)
		return -1;
	int ret=0;
	char *d1 = fileAlloc(file1, (int*)&s1);
	char *d2 = fileAlloc(file2, (int*)&s2);
	if (s1 != s2) {
		ret = -1;
	} else {
		for (size_t i=0; i<s1; i++) {
			if (d1[i] != d2[i])
				ret = -1;
		}
	}
	if (d1)
		free(d1);
	if (d2)
		free(d2);
	return ret;
}

// filename = c:\path\file.ext
// destpath = \\symserv\data\symserv\2week\File.ext\01234\File.ext
int safeModeStore(const char *filename, const char *destpath)
{
	int ret = 0;
	char shortname[MAX_PATH];
	char infofile[MAX_PATH];
	char datafile[MAX_PATH];
	char lockfile[MAX_PATH];
	char globallogfile[MAX_PATH];
	const char *cs = destpath + strlen(destpath);
	int eat;
	for (eat=4; eat>0 && cs > destpath; cs--)
	{
		if (*cs=='\\')
			eat--;
		if (eat == 0) {
			cs++;
			break;
		}
	}
	if (eat==0) {
		// good
	} else {
		// destpath must be f:\File.ext\01234\File.ext
		cs = destpath;
	}
	strcpy_s(shortname, cs);
	char *s = strrchr(shortname, '\\');
	if (s && (strstr(shortname, s)!=s)) { // file.ext is in there twice
		*s = '\0';
	}
	for (s=shortname; *s; s++)
		if (!isalnum(*(unsigned char*)s))
			*s = '_';
	sprintf_s(infofile, "%s%s.info", backup_store, shortname);
	sprintf_s(datafile, "%s%s.data", backup_store, shortname);
	sprintf_s(lockfile, "%sglobal.lock", backup_store);
	sprintf_s(globallogfile, "%sglobal.log", backup_store);
	int lock = gimmeWaitToAcquireLock(lockfile);
	if (fileExists(infofile) && fileExists(datafile) && fileCompare(datafile, filename)==0)
	{
		// Already there!  Do nothing.
		g_did_a_file = true;
	} else {
		// Needs updating
		// Write info first, then data
		FILE *f = NULL;
		if (0 == fopen_s(&f, infofile, "wb"))
		{
			fprintf(f, "1\n%s\n%s\n%d\n%s\n", filename, destpath, fileSize(filename), storepath);
			fclose(f);

			// Copy data
			printf("Copying to backup store... ");

			if (CopyFile(filename, datafile, TRUE)) {
				printf("Added to backup store.\n");
				g_did_a_file = true;
				// Log this
				logf(globallogfile, "%s: Added to backup store: \n\t%s\n\t%s\n\t%d\n\t%s\n", 
					getComputerName(), filename, destpath, fileSize(filename), storepath);
			} else {
				_unlink(datafile);
				errorf("FAILED to copy to %s.\n", datafile);
				ret = -11;
			}
		} else {
			errorf("Unable to open backup info file %s\n", infofile);
			ret = -12;
		}
	}
	gimmeUnaquireAndDeleteLock(lock, lockfile);
	return ret;
}

typedef struct CopyData
{
	char *src;
	char *dest;
	bool move;
	volatile bool done;
	volatile BOOL ret;
} CopyData;

void __cdecl copyAsyncThread(void *data)
{
	CopyData *cd = (CopyData*)data;
	if (cd->move) {
		cd->ret = MoveFile(cd->src, cd->dest);
	} else {
		cd->ret = CopyFile(cd->src, cd->dest, TRUE);
	}
	cd->done = true;
	_endthread();
}

typedef enum CopyRet
{
	CopyRet_OK,
	CopyRet_CopyFailed,
	CopyRet_Timeout,
} CopyRet;
CopyRet CopyFileAsync(const char *src, const char *dest, bool move, unsigned int time_millis)
{
	CopyData *cd = (CopyData*)calloc(sizeof(CopyData), 1);
	cd->src = _strdup(src);
	cd->dest = _strdup(dest);
	cd->move = move;
	cd->done = false;
	_beginthread(copyAsyncThread, 64*1024, cd);
	int t = timeGetTime();
	while (!cd->done && (timeGetTime() - t < time_millis))
	{
		Sleep(1);
	}
	if (cd->done)
	{
		return cd->ret?CopyRet_OK:CopyRet_CopyFailed;
	} else {
		// Kill thread?
		return CopyRet_Timeout;
	}
}

int storeFile(char *filename, char *filenameForExt)
{
	if (_stricmp(filename, "--removeSource")==0) {
		g_remove_source = true;
		return 0;
	}

	if (strStartsWith(filename, "/r:")) {
		char *dirname = filename + 3;
		return storeFileRecurse(dirname);
	}

	if (!fileExists(filename))
		return 0; // Message printed out earlier

	printf("%s: ", filename);
	g_filename = filename;

	// Get identifier from EXE or PDB
	char key[MAX_PATH];
	bool bGood=false;
	int ret = 0;

	printf("Loading ");

	if (strEndsWith(filenameForExt, ".pdb")) {

		DWORD64 base;

		//HANDLE hFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		//assert(hFile);
		//if (base = SymLoadModuleEx(hProcess, hFile, NULL, NULL, 
		//	0x40000000, (DWORD)fileSize(filename), NULL, 0))
		if (base = SymLoadModuleEx(hProcess, NULL, filename, NULL, 
			0x40000000, (DWORD)fileSize(filename), NULL, 0))
		{
			IMAGEHLP_MODULE64 mi = {0};
			mi.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
			if (SymGetModuleInfo64(hProcess, base, &mi)) {
				// Construct a symstore name now
				if (mi.PdbSig70.Data1 == 0) {
					sprintf_s(key, "%08X%x", mi.PdbSig, mi.PdbAge);
				} else {
					sprintf_s(key, "%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%x", mi.PdbSig70.Data1, mi.PdbSig70.Data2, mi.PdbSig70.Data3, mi.PdbSig70.Data4[0], mi.PdbSig70.Data4[1], mi.PdbSig70.Data4[2], mi.PdbSig70.Data4[3], mi.PdbSig70.Data4[4], mi.PdbSig70.Data4[5], mi.PdbSig70.Data4[6], mi.PdbSig70.Data4[7], mi.PdbAge);
				}
				bGood = true;
			} else {
				HRESULT error = GetLastError();
				errorf("SymGetModuleInfo64 returned error : %d.\n", error);
				ret = -5;
			}

			SymUnloadModule64(hProcess, base);

		}
		else
		{
			HRESULT error = GetLastError();
			errorf("SymLoadModuleEx returned error : %d.\n", error);
			ret = -6;
		}

		//CloseHandle(hFile);
	} else {
		// EXE, DLL, etc
		DWORD timestamp, sizeOfImage;
		if (getEXEInfo(filename, &timestamp, &sizeOfImage))
		{
			bGood = true;
			sprintf_s(key, "%X%x", timestamp, sizeOfImage);
		} else {
			errorf("getEXEInfo failed.\n");
			ret = -7;
		}
	}

	if (bGood)
	{
		char final_path[MAX_PATH];
		strcpy_s(final_path, storepath);
		backSlashes(filenameForExt);
		char *s = strrchr(filenameForExt, '\\');
		const char *shortname = filenameForExt;
		char globallogfile[MAX_PATH];

		if (s)
			shortname = s+1;
		strcat_s(final_path, shortname);
		strcat_s(final_path, "\\");
		strcat_s(final_path, key);
		strcat_s(final_path, "\\");
		strcat_s(final_path, shortname);

		sprintf_s(globallogfile, "%s\\" SYMBOL_ERROR_LOG_FILENAME, storepath);

#if 0
		// Check if this is a symstore->symstore move, that the keys are identical
		if (strStartsWith(filename, "\\\\symserv\\data\\symserv\\")) {
			char *s = filename + strlen("\\\\symserv\\data\\symserv\\");
			s = strchr(s, '\\'); // skip project
			if (s) {
				s++;
				if (strStartsWith(s, shortname)) {
					s = strchr(s, '\\');
					if (s) {
						s++;
						assert(strStartsWith(s, key));
						assert(s[strlen(key)] == '\\');
					}
				}
			}
		}
#endif

		if (!g_server_down)
		{
			bool bExists = false;
			int lockhandle;
			char lockname[MAX_PATH];
			printf("Querying ");
			if (_stricmp(action, "add")==0)
			{
				strcpy_s(lockname, final_path);
				s = strrchr(lockname, '\\');
				*s = '\0';
				s = strrchr(lockname, '\\')+1;
				*s = '\0';
				strcat_s(lockname, "CrypticSymStore.lock");
				lockhandle = gimmeWaitToAcquireLock(lockname);
			}

			bool bWrongSize = false;
			if (fileExists(final_path)) {
				bExists = true;
				if (fileSize(final_path) != fileSize(filename))
					bWrongSize = true;
			} else {
				char compressed[MAX_PATH];
				strcpy_s(compressed, final_path);
				compressed[strlen(compressed)-1] = '_';
				if (fileExists(compressed)) {
					bExists = true;
					logf(globallogfile, "Attempt to symstore from %s (size %d) to %s when compressed file %s already exists (%s)",
						filename, fileSize(filename),
						final_path,
						compressed,
						getLocalHostNameAndIPs());
					strcpy_s(final_path, compressed);
				}
			}
			if (_stricmp(action, "query")==0) {
				// Query
				if (bExists) {
					if (bWrongSize)
					{
						printf("Exists, but wrong size!  %s\n", final_path);
					} else {
						printf("%s\n", final_path);
					}
				} else {
					printf("Not found in symstore.  Key is %s.\n", key);
				}
			} else {
				// LOCK
				// Add
				bool bRemoveMe = false;

				printf(" (key %s)", key);

				if (bExists && bWrongSize) {
					char old[MAX_PATH];
					bExists = false;
					logf(globallogfile, "Replacing symbol file with one of a different size: %s (%s)", final_path, getLocalHostNameAndIPs()); 
					sprintf(old, "%s.old", final_path);
					rename(final_path, old);
				}
				if (bExists) {
					printf("Already in symstore.\n");
					g_did_a_file = true;
					bRemoveMe = true;
				} else {
					MakeSureDirectoryPathExists(final_path);
					if (g_remove_source)
						printf("Moving ");
					else
						printf("Copying ");
					CopyRet cr = CopyFileAsync(filename, final_path, g_remove_source, COPY_TIMEOUT);
					if (cr == CopyRet_OK) {
						printf("Added.\n");
						bRemoveMe = true;
						g_did_a_file = true;
					} else {
						_unlink(final_path);
						if (cr == CopyRet_Timeout) {
							errorf("TIMEOUT doing %s to %s.\n", g_remove_source?"move":"copy", final_path);
						} else {
							errorf("FAILED to %s to %s.\n", g_remove_source?"move":"copy", final_path);
						}
						g_server_down = true;
						ret = -8;
					}
				}
				if (bRemoveMe && g_remove_source) {
					char path[MAX_PATH];
					char extrafile[MAX_PATH];
					printf("Removing source files and folder... ");

					strcpy_s(path, filename);
					s = strrchr(path, '\\');
					*s = '\0';
					if (fileExists(filename)) {
						if (0!=_unlink(filename))
							printf("Failed to remove %s.  ", filename);
					}
					sprintf_s(extrafile, "%s\\refs.ptr", path);
					if (fileExists(extrafile)) {
						if (0!=_unlink(extrafile))
							printf("Failed to remove %s.  ", extrafile);
					}
					sprintf_s(extrafile, "%s\\.rights", path);
					if (fileExists(extrafile)) {
						if (0!=_unlink(extrafile))
							printf("Failed to remove %s.  ", extrafile);
					}

					foldersToRemove.push_back(_strdup(path));
					printf("Done.\n");
				}
				// Unlock
				gimmeUnaquireAndDeleteLock(lockhandle, lockname);
			}
		}
		if (g_server_down && !g_doing_merge)
		{
			ret = safeModeStore(filename, final_path);
		}
	}
	return ret;
}

void safeStoreMergeFile(const char *filename)
{
	char data_filename[MAX_PATH];
	char globallogfile[MAX_PATH];
	strcpy_s(data_filename, filename);
	char *s = strrchr(data_filename, '.');
	strcpy_s(s, MAX_PATH - (s - data_filename), ".data");
	printf("Merging old file from backup store: %s\n", filename);

	sprintf_s(globallogfile, "%sglobal.log", backup_store);

	logf(globallogfile, "%s: Merging from backup store: %s size %d", 
		getComputerName(), filename, fileSize(filename));

	int data_size;
	char *data = fileAlloc(filename, &data_size);
	if (!data) {
		errorf("info file could not be read: %s\n", filename);
		logf(globallogfile, "info file could not be read: %s\n", filename);
		return;
	}

	if (!strStartsWith(data, "1\n")) {
		errorf("info file is bad version: %s\n", filename);
		logf(globallogfile, "info file is bad version: %s\n", filename);
		return;
	}

	s = strchr(data, '\n') + 1;
	char *orig_fname = s;
	s = strchr(s, '\n');
	if (!s) {
		errorf("error parsing info file: %s\n", filename);
		return;
	}
	*s++ = '\0';
	char *dest_fname = s;
	s = strchr(s, '\n');
	if (!s) {
		errorf("error parsing info file: %s\n", filename);
		return;
	}
	*s++ = '\0';
	int source_size = atoi(s);
	s = strchr(s, '\n');
	if (!s) {
		errorf("error parsing info file: %s\n", filename);
		return;
	}
	*s++ = '\0';
	char *new_store_path = s;
	s = strchr(s, '\n');
	if (!s) {
		errorf("error parsing info file: %s\n", filename);
		return;
	}
	*s++ = '\0';
	strcpy_s(storepath, new_store_path);
	action = "add";

	if (!dirExists(storepath))
	{
		errorf("Store does not exist, skipping storing of saved symbols: %s\n", filename);
		logf(globallogfile, "%s: Store %s does not exist, skipping storing of saved symbols: %s", 
			getComputerName(), storepath, filename);
		return;
	}

	if (fileSize(data_filename) != source_size)
	{
		errorf("data file did not match recorded size: %s\n", filename);
		return;
	}
	printf("  Orig name: %s\n  Dest name: %s\n  File size: %d\n", orig_fname, dest_fname, source_size);

	// Store it
	if (0 == storeFile(data_filename, orig_fname))
	{
		logf(globallogfile, "%s: Merge succeeded: %s", 
			getComputerName(), filename);
		// Success!
		_unlink(filename);
		_unlink(data_filename);
	}

	free(data);
}

void safeStoreMergeFiles()
{
	char lockfile[MAX_PATH];
	int startt = timeGetTime();
	sprintf_s(lockfile, "%sglobal.lock", backup_store);
	int lockhandle = gimmeAcquireLock(lockfile);
	if (lockhandle < 0) // Someone else must be doing it!
		return;

	g_doing_merge = true;

	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;

	char dirspec[MAX_PATH];
	sprintf_s(dirspec, "%s*.info", backup_store);

	// Find the first file in the directory.
	hFind = FindFirstFile(dirspec, &FindFileData);

	if (hFind != INVALID_HANDLE_VALUE) 
	{
		do
		{
			char path[MAX_PATH];
			if (timeGetTime() - startt > TIMEOUT_MERGING)
				break;
			sprintf_s(path, "%s%s", backup_store, FindFileData.cFileName);
			safeStoreMergeFile(path);
		} while (FindNextFile(hFind, &FindFileData) != 0 && !g_server_down);
		FindClose(hFind);
	}

	gimmeUnaquireAndDeleteLock(lockhandle, lockfile);
}

//////////////////////////////////////////////////////////////////////////
// Symstore compressor
static ULARGE_INTEGER ularge_now;
static bool do_prune=false;
static volatile LONG scan_done=0;
static volatile LONG scanned_files=0;
static volatile LONG pruned_files=0;
static volatile LONG scanned_directories=0;
static volatile LONG filequeue_size=0;
CRITICAL_SECTION csFileQueue;
typedef struct FileQueue
{
	struct FileQueue *next;
	char *filename;
} FileQueue;
FileQueue *head, *tail;

void queueFile(const char *filename)
{
	FileQueue *fq = new FileQueue;
	fq->filename = _strdup(filename);
	fq->next = NULL;
	EnterCriticalSection(&csFileQueue);
	if (!head)
	{
		head = tail = fq;
	} else {
		tail->next = fq;
		tail = fq;
	}
	InterlockedIncrement(&filequeue_size);
	LeaveCriticalSection(&csFileQueue);
}

void scanRecurse(char *path, int prune_days)
{
	WIN32_FIND_DATA ffd;
	HANDLE hFind;
	DWORD dwError=0;
	char newpath[MAX_PATH];

	strcpy_s(newpath, path);
	if (newpath[strlen(newpath)-1]!='\\')
		strcat_s(newpath, "\\");

	if (do_prune)
	{
		// Look for prune.txt
		char prunepath[MAX_PATH];
		strcpy_s(prunepath, newpath);
		strcat_s(prunepath, "prune.txt");
		FILE *f;
		if (0==fopen_s(&f, prunepath, "rt"))
		{
			int days;
			if (1==fscanf_s(f, "%d", &days))
			{
				prune_days = days;
			} else {
				errorf("%s exists, but the first token was not an integer", prunepath);
			}
			fclose(f);
		}
	}

	strcat_s(newpath, "*");
	hFind = FindFirstFile(newpath, &ffd);

	if (INVALID_HANDLE_VALUE == hFind) 
	{
		return;
	} 

	do
	{
		if (path[strlen(path)-1] == '\\')
			sprintf_s(newpath, "%s%s", path, ffd.cFileName);
		else
			sprintf_s(newpath, "%s\\%s", path, ffd.cFileName);

		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (ffd.cFileName[0] != '.')
				scanRecurse(newpath, prune_days);
		}
		else
		{
			char *ext = strrchr(newpath, '.');
			if (!ext) {
				// no extension, ignore
			} else if (
				_stricmp(ext, ".exe")==0 ||
				_stricmp(ext, ".dll")==0 ||
				_stricmp(ext, ".pdb")==0 ||
				_stricmp(ext, ".ex_")==0 ||
				_stricmp(ext, ".dl_")==0 ||
				_stricmp(ext, ".pd_")==0)
			{
				bool bPrune=false;
				if (prune_days > 0)
				{
					LARGE_INTEGER modtime;
					modtime.HighPart = ffd.ftCreationTime.dwHighDateTime;
					modtime.LowPart = ffd.ftCreationTime.dwLowDateTime;
					LARGE_INTEGER diff;
					diff.QuadPart = ularge_now.QuadPart - modtime.QuadPart;
					if (diff.QuadPart < 0)
					{
						// Time-traveling file, do nothing!
					} else {
						if (diff.QuadPart > prune_days * 864000000000LL)
						{
							// it's old, prune it!
							bPrune = true;
						}
					}
				}
				if (bPrune)
				{
					InterlockedIncrement(&pruned_files);
					_unlink(newpath);
					char *s = strrchr(newpath, '\\');
					assert(s);
					*s = '\0';
					if (0!=_rmdir(newpath))
					{
						errorf("could not remove folder %s", newpath);
					}
				} else {
					if (ext[strlen(ext)-1] != '_')
					{
						queueFile(newpath);
					}
				}
			} else {
				//printf("Unknown file type:%s\n", newpath);
			}
			InterlockedIncrement(&scanned_files);
		}
	}
	while (FindNextFile(hFind, &ffd) != 0);
	InterlockedIncrement(&scanned_directories);
}

void _cdecl scanThread(void *param)
{
	scanRecurse(storepath, -1);
	scan_done = 1;
}

HANDLE spawnProcess(char *cmd)
{
	STARTUPINFO si = {0};
	PROCESS_INFORMATION pi = {0};
	si.cb = sizeof(si);
	if(1) // minimized || hidden)
	{
		si.dwFlags |= STARTF_USESHOWWINDOW;
		si.wShowWindow = (/*hidden*/1 ? SW_HIDE : SW_MINIMIZE);
	}

	if (!CreateProcess(NULL, cmd,
		NULL, // process security attributes, cannot be inherited
		NULL, // thread security attributes, cannot be inherited
		FALSE, // do NOT let this child inherit handles
		CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP,
		NULL, // inherit environment
		NULL, // inherit current directory
		&si,
		&pi))
	{
		printf("Error creating process '%s'\n", cmd);
		return INVALID_HANDLE_VALUE;
	} else {
		CloseHandle(pi.hThread);
		return pi.hProcess;
	}
}

bool processStillRunning(HANDLE h)
{
	HRESULT hr = WaitForSingleObject(h, 0);
	if (hr == WAIT_OBJECT_0)
	{
		return false;
	}
	return true;
}

struct {
	char lockname[MAX_PATH];
	int count;
	int handle;
} locks[16];

int getlockpooled(const char *path)
{
	int firstfree=-1;
	for (int i=0; i<ARRAYSIZE(locks); i++)
	{
		if (locks[i].count)
		{
			if (_stricmp(locks[i].lockname, path)==0)
			{
				locks[i].count++;
				return i;
			}
		} else {
			if (firstfree==-1)
				firstfree = i;
		}
	}
	strcpy_s(locks[firstfree].lockname, path);
	locks[firstfree].count=1;
	locks[firstfree].handle = gimmeWaitToAcquireLock(locks[firstfree].lockname);
	return firstfree;
}

void releaselockpooled(int handle)
{
	assert(locks[handle].count>0);
	locks[handle].count--;
	if (!locks[handle].count)
	{
		gimmeUnaquireAndDeleteLock(locks[handle].handle, locks[handle].lockname);
	}
}

struct {
	HANDLE h;
	char *filename;
	char outpath[MAX_PATH];
	char localpath[MAX_PATH];
	int lockhandle;
	int timestamp;
	char status[1024];
} process_info[16];
int num_processes=4;
bool do_random = true; // Select randomly from list of available files, prevents locking the same folder for too long
int num_compressed;

bool makeLocalCopy(int index, const char *remotepath, char local_path[MAX_PATH], int *err)
{
	bool success;
	sprintf_s(local_path, MAX_PATH, "C:\\TEMP\\CrypticSymStore\\%d-%d", _getpid(), index);
	_mkdir("C:\\TEMP");
	_mkdir("C:\\TEMP\\CrypticSymStore");
	_mkdir(local_path);

	const char *s = strrchr(remotepath, '\\');
	assert(s);
	strcat_s(local_path, MAX_PATH, s);

	success = (bool)CopyFile(remotepath, local_path, FALSE);
	if (!success)
		*err = GetLastError();
	return success;
}

int doCompress(bool prune)
{
	char globallogfile[MAX_PATH];
	FILETIME filetime_now;

	do_prune = prune;
	GetSystemTimeAsFileTime(&filetime_now);
	ularge_now.HighPart = filetime_now.dwHighDateTime;
	ularge_now.LowPart = filetime_now.dwLowDateTime;

	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	bool bDone = false;
	int y0=2;
	int startTime = timeGetTime();
	InitializeCriticalSection(&csFileQueue);
	printf("Scanning %s for files to compress and starting compression...\n", storepath);
	_beginthread(scanThread, 0, NULL);

	sprintf_s(globallogfile, "%s\\"SYMBOL_ERROR_LOG_FILENAME, storepath);

	while (!bDone)
	{
		bool bStartedOneThisTick=false;
		bDone = true;
		if (!scan_done)
			bDone = false;
		if (filequeue_size)
			bDone = false;
		int pending=0;
		for (int i=0; i<num_processes; i++)
		{
			//process_info[i].status[0] = 0;
			if (process_info[i].filename == NULL)
			{
				if (bStartedOneThisTick)
				{
					// Don't start a new one, leave status alone
				}
				else if (filequeue_size)
				{
					// start a new one
					FileQueue *fq;
					EnterCriticalSection(&csFileQueue);
					if (do_random)
					{
						FileQueue *prev=NULL;
						int index = rand() % filequeue_size;
						fq = head;
						while (index)
						{
							prev = fq;
							fq = fq->next;
							index--;
						}
						if (fq == head)
						{
							head = head->next;
						} else {
							prev->next = fq->next;
						}
						if (fq == tail)
						{
							tail = prev;
						}
					} else {
						fq = head;
						head = head->next;
					}
					InterlockedDecrement(&filequeue_size);
					LeaveCriticalSection(&csFileQueue);

					strcpy_s(process_info[i].outpath, fq->filename);
					process_info[i].outpath[strlen(process_info[i].outpath)-1] = '_';
					if (fileExists(process_info[i].outpath))
					{
						char old_uncompressed[MAX_PATH];
						// already exists, just delete input file
						logf(globallogfile, "Compressed file %s already exists.  How did this happen? (%s)", fq->filename, getLocalHostNameAndIPs());
						sprintf(old_uncompressed, "%s.unc", fq->filename);
						rename(fq->filename, old_uncompressed);
						free(fq->filename);
					} else if (!fileExists(fq->filename)) {
						// source no longer exists
						// ignore it
						free(fq->filename);
					} else {
						// process it
						bool success;
						int err;
						char cmd[1024];
						char lockname[MAX_PATH];

						// Lock folder
						strcpy_s(lockname, fq->filename);
						char *s = strrchr(lockname, '\\');
						*s = '\0';
						s = strrchr(lockname, '\\')+1;
						*s = '\0';
						strcat_s(lockname, "CrypticSymStore.lock");
						process_info[i].lockhandle = getlockpooled(lockname);
						process_info[i].filename = fq->filename;

						// Copy file local
						success = makeLocalCopy(i, fq->filename, process_info[i].localpath, &err);
						if (!success)
							logf(globallogfile, "Unable to make local copy of %s  at %s: %d (%s)", fq->filename, process_info[i].localpath, err, getLocalHostNameAndIPs());

						// Unlock
						releaselockpooled(process_info[i].lockhandle);

						// Compress to remote path
						if (success)
						{
							sprintf_s(cmd, "MakeCAB /D CompressionType=LZX \"%s\" \"%s\"", process_info[i].localpath, process_info[i].outpath);
							process_info[i].h = spawnProcess(cmd);
							process_info[i].timestamp = timeGetTime();
							strcpy_s(process_info[i].status, "Started");
							bStartedOneThisTick = true;
						}
					}
					delete fq;
					pending++;
				} else {
					strcpy_s(process_info[i].status, "Idle");
				}
			} else {
				// one running
				float elapsed = (timeGetTime() - process_info[i].timestamp)/1000.f;
				if (!processStillRunning(process_info[i].h))
				{
					// finished!
					if (fileExists(process_info[i].outpath))
					{
						DeleteFile(process_info[i].filename);
					} // otherwise failure, leave it be until the next time this is run
					DeleteFile(process_info[i].localpath);
					free(process_info[i].filename);
					CloseHandle(process_info[i].h);
					process_info[i].filename = NULL;
					sprintf_s(process_info[i].status, "Done (%1.1fs)", elapsed);
					num_compressed++;
				} else {
					sprintf_s(process_info[i].status, "Waiting %2.0fs", elapsed);
					bDone = false;
					pending++;
				}
			}
		}

		
		char buf[1024];
		sprintf_s(buf, "%1.0fs, %d/%d processed, %d scanned, %d pruned%s                          ",
			(timeGetTime() - startTime)/1000.f,
			num_compressed, num_compressed+filequeue_size+pending,
			scanned_files,
			pruned_files,
			scan_done?"":" (still scanning)");

		COORD pos = {0, y0};
		DWORD dwTemp;
		SetConsoleCursorPosition(hConsole, pos);
		WriteConsole(hConsole, buf, (int)strlen(buf), &dwTemp, NULL);

		for (int i=0; i<num_processes; i++)
		{
			sprintf_s(buf, " %d: [%s] ", i, process_info[i].status);
			if (process_info[i].filename)
			{
				char *s1 = process_info[i].filename;
				char *s2 = storepath;
				while (*s1 == *s2 && *s1)
				{
					s1++; s2++;
				}
				strcat_s(buf, s1);
				if (strlen(buf) > 79)
				{
					buf[79] = '\0';
				}
			}
			while (strlen(buf) < 79)
			{
				strcat_s(buf, " ");
			}
			pos.Y = y0+i+1;
			SetConsoleCursorPosition(hConsole, pos);
			WriteConsole(hConsole, buf, (int)strlen(buf), &dwTemp, NULL);
		}
		Sleep(100);
	}
	printf("\n\nFinished processing %d files in %1.fs\n", num_compressed, (timeGetTime() - startTime)/1000.f);
	return 0;
}

int main(int argc, char **argv)
{
	char **localfiles;
	int file_count;
	int ret=0;
	if (argc<3 || ((_stricmp(argv[2], "compress")!=0 && _stricmp(argv[2], "compressAndPrune")!=0) && argc<4) || _stricmp(argv[2], "add")!=0 && _stricmp(argv[2], "query")!=0 && _stricmp(argv[2], "compress")!=0 && _stricmp(argv[2], "compressAndPrune")!=0)
	{
		printf("Usage: %s \\\\symserv\\path\\to\\store (query|add|compress|compressAndPrune) [options] filename1 [filename2]\n", argv[0]);
		printf("  filename can be in the form of /r:dirname\n");
		printf("  options must come before filenames, and can be one of:\n");
		printf("    --removeSource - Deletes the source file and containing folder upon copying.\n");
		printf("    --safe - Stores to an alternative store if the server is unresponsive (default).\n");
		printf("    --unsafe - Disables --safe mode.\n");
		printf("  compress will compress all relevant files in the specified folder recursively\n");
		printf("    (requires no filename)\n");
		printf("    --1 --2 --4 --6 --8 - number of different compression processes to run at once.\n");
		printf("  compressAndPrune will compress and also prune old files (based on prune.txt in\n");
		printf("    a parent folder, containing # of days to keep a file)\n");
		return 1;
	}
	if (_stricmp(argv[2], "verifylinkdependencies")==0)
	{
		// Never actually called
		// Just calling this because it requires the new version of dbghelp.dll, which we need!
		char buf1[MAX_PATH], buf2[MAX_PATH];
		SymGetSymbolFile(0, NULL, NULL, sfPdb, buf1, MAX_PATH, buf2, MAX_PATH);
	}
	for (int i=0; i<argc; i++)
	{
		if (_stricmp(argv[i], "--safe")==0)
			g_safe = true;
		if (_stricmp(argv[i], "--unsafe")==0)
			g_safe = false;
		if (_stricmp(argv[i], "--1")==0)
			num_processes = 1;
		if (_stricmp(argv[i], "--2")==0)
			num_processes = 2;
		if (_stricmp(argv[i], "--4")==0)
			num_processes = 4;
		if (_stricmp(argv[i], "--6")==0)
			num_processes = 6;
		if (_stricmp(argv[i], "--8")==0)
			num_processes = 8;
	}
	strcpy_s(storepath, argv[1]);
	if (!strEndsWith(storepath, "\\"))
		strcat_s(storepath, "\\");
	action = argv[2];
	localfiles = &argv[3];
	file_count = argc - 3;
	backSlashes(storepath);
	printf("Checking existence of store...\r");
	if (!dirExists(storepath)) {
		if (_stricmp(action, "query")==0 || _stricmp(action, "compress")==0 || _stricmp(action, "compressAndPrune")==0) {
			errorf("Store directory \"%s\" not found.\n", storepath);
			return -2;
		} else {
			// Make it and parents exist
			MakeSureDirectoryPathExists(storepath);
		}
	}
	if (!dirExists(storepath)) {
		if (!g_safe)
		{
			errorf("Store directory \"%s\" could not be created.\n", storepath);
			return -3;
		} else {
			errorf("Store directory \"%s\" could not be created, storing to backup store.\n", storepath);
			g_server_down = true;
		}
	}
	if (_stricmp(action, "add")==0 && !g_server_down)
	{
		// Create pingme.txt for Visual Studio
		char pingme[MAX_PATH];
		sprintf_s(pingme, "%s\\pingme.txt", storepath);
		if (!fileExists(pingme))
		{
			FILE *f=NULL;
			if (0 == fopen_s(&f, pingme, "wb"))
				fclose(f);
		}
	}
	if (_stricmp(action, "compressAndPrune")==0)
	{
		return doCompress(true);
	}
	if (_stricmp(action, "compress")==0)
	{
		return doCompress(false);
	}

	for (int i=0; i<file_count; i++) {
		if (!strStartsWith(localfiles[i], "--") && (strStartsWith(localfiles[i], "/r:") && !dirExists(localfiles[i]+3) || !strStartsWith(localfiles[i], "/r:") && !fileExists(localfiles[i])))
		{
			printf("Local file \"%s\" does not exist.\n", localfiles[i]);
		}
	}

	DWORD  error;

	printf("SymInitialize...\r");
	SymSetOptions(SYMOPT_UNDNAME); // | SYMOPT_DEFERRED_LOADS);

	hProcess = (HANDLE)12345; // GetCurrentProcess();
	// hProcess = (HANDLE)processId;

	if (SymInitialize(hProcess, NULL, FALSE))
	{
		// SymInitialize returned success
	}
	else
	{
		// SymInitialize failed
		error = GetLastError();
		errorf("SymInitialize returned error : %d\n", error);
		return -4;
	}

	printf("Storing files...\r");
	for (int i=0; i<file_count; i++) 
	{
		int r = storeFile(localfiles[i], localfiles[i]);
		if (r!=0)
			ret = r;
	}

	for (unsigned int i=0; i<foldersToRemove.size(); i++) 
	{
		if (0!=_rmdir(foldersToRemove[i])) {
			int er = errno;
			printf("Failed to rmdir %s.\n", foldersToRemove[i]);
		}
		// Try parent too
		char *s = strrchr(foldersToRemove[i], '\\');
		if (s) {
			*s = '\0';
			_rmdir(foldersToRemove[i]);
		}
	}

	if (!g_did_a_file && ret==0 && _stricmp(action, "add")==0)
	{
		errorf("Error: did nothing, no specified files existed or specified folder\n\tcontained no appropriate files.");
		ret = -9;
	} else {
		if (!g_server_down)
		{
			g_safe = false; // Don't re-safe-store something we're already dealing with!
			// Check for backup store files that need to be moved into the server
			safeStoreMergeFiles();
		}
	}

	return ret;
}