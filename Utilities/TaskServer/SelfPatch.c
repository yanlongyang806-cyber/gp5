#include "SelfPatch.h"
#include "sysutil.h"
#include "crypt.h"
#include "file.h"
#include "mathutil.h"
#include "error.h"
#include "logging.h"
#include "process_util.h"
#include <psapi.h>
#include "utils.h"
#include "UTF8.h"

static void checksumFile(char *fname,Checksum *check)
{
	U8			*chunk_mem;
	U32			size,chunk_size,i;
	FILE		*file;
#define CHUNK_SIZE (1<<20)

	cryptMD5Init();
	size = fileSize(fname);
	chunk_mem = malloc(CHUNK_SIZE);
	file = fopen(fname,"rb");
	if (!file)
		goto fail;
	for(i=0;i<size;i+=CHUNK_SIZE)
	{
		chunk_size = MIN(CHUNK_SIZE,size - i);
		if (fread(chunk_mem,1,chunk_size,file) != chunk_size)
			goto fail;
		cryptMD5Update(chunk_mem,chunk_size);
	}
	cryptMD5Final(check->values);
	check->size = size;
fail:
	fclose(file);
	free(chunk_mem);
}

static void safeRenameFile(char *oldnamep,char *newnamep)
{
	char	oldname[MAX_PATH],newname[MAX_PATH];
	int		i;

	strcpy(oldname,oldnamep);
	strcpy(newname,newnamep);
	backSlashes(oldname);
	backSlashes(newname);
	chmod(oldname,_S_IREAD | _S_IWRITE);
	if (rename(oldname,newname) == 0)
		return;
	chmod(newname,_S_IREAD | _S_IWRITE);
	if (unlink(newname) != 0)
	{
		if (strEndsWith(newname,".exe") || strEndsWith(newname,".tmp"))
		{
			char	*s,new_exename[MAX_PATH];

			strcpy(new_exename,newname);
			s = strrchr(new_exename,'.');
			strcpy_s(s,5,".old");
			safeRenameFile(newname,new_exename);
		}
		else if (fileExists(newname))
			FatalErrorf("ErrRenameDelete %s",newname);
	}
	for(i=0;i<100;i++)
	{
		if (rename(oldname,newname) == 0)
			return;
		Sleep(100);
	}
	FatalErrorf("ErrRenameFailed %s %s",oldname,newname);
}

static void safeUnlink(char *fname)
{
	int		i;

	for(i=0;i<100;i++)
	{
		if (!fileExists(fname))
			return;
		chmod(fname,_S_IREAD|_S_IWRITE);
		if (unlink(fname) == 0)
			return;
		Sleep(100);
	}
}

static void execAndQuit(char *exe_name)
{
	STARTUPINFO			si = {0};
	PROCESS_INFORMATION	pi = {0};
	int					result;
	si.cb				= sizeof(si);

	result = CreateProcess_UTF8(exe_name, (char*)GetCommandLine(),
		NULL,			NULL,			FALSE,			CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP,
		NULL,			NULL,			&si,			&pi);
	assert(result);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	logWaitForQueueToEmpty();
	exit(0);
}


void selfPatchStartup(Checksum *checksum)
{
	U8			new_name[MAX_PATH],tmp_name[MAX_PATH];
	U8			exe_name[MAX_PATH],old_name[MAX_PATH];

	strcpy(exe_name,getExecutableName());
	backSlashes(exe_name);
	changeFileExt(exe_name, ".new", new_name);
	changeFileExt(exe_name, ".tmp", tmp_name);
	changeFileExt(exe_name, ".old", old_name);
	if (strEndsWith(exe_name,".tmp"))
	{
		safeUnlink(old_name);

		strcpy_s(exe_name + strlen(exe_name)-3,4,"exe");
		safeRenameFile(new_name,exe_name);
		execAndQuit(exe_name);
	}
	safeUnlink(old_name);
	safeUnlink(new_name);
	safeUnlink(tmp_name);

	checksumFile(exe_name, checksum);
}

static void writeFile(const char *fname, const char *how, const void *mem, U32 len)
{
	FILE	*file;

	chmod(fname,_S_IREAD|_S_IWRITE);
	file = fopen(fname,how);
	if (!file)
		return;
	fwrite(mem,1,len,file);
	fclose(file);
	chmod(fname,_S_IREAD);
}



#define TASKSERVER_32BIT_EXE "TaskServer.exe"
#define TASKSERVER_64BIT_EXE "TaskServerX64.exe"

bool LowestPID(void)
{
	// Get the list of process identifiers.
	DWORD aProcesses[1024], cbNeeded, cProcesses;
	unsigned int i;
	int count = 0;
	DWORD lowestPID=0;

	if ( !EnumProcesses( aProcesses, sizeof(aProcesses), &cbNeeded ) )
		return 0;

	// Calculate how many process identifiers were returned.

	cProcesses = cbNeeded / sizeof(DWORD);

	// Print the name and process identifier for each process.

	for ( i = 0; i < cProcesses; i++ )
	{
		if(ProcessNameMatch( aProcesses[i] , TASKSERVER_32BIT_EXE, true))
		{
			if (!lowestPID || aProcesses[i]<lowestPID)
				lowestPID = aProcesses[i];
			count++;
		}
	}
	if (GetCurrentProcessId()==lowestPID)
		return true;
	return false;
}

void selfPatch(const void *new_exe_data, int new_exe_data_size)
{
	U8 new_name[MAX_PATH],tmp_name[MAX_PATH];
	U8 exe_name[MAX_PATH];
	strcpy(exe_name,getExecutableName());
	backSlashes(exe_name);
	changeFileExt(exe_name, ".new", new_name);
	changeFileExt(exe_name, ".tmp", tmp_name);

	do 
	{
		if (LowestPID())
		{
			writeFile(new_name,"!wb",new_exe_data,new_exe_data_size);
			writeFile(tmp_name,"!wb",new_exe_data,new_exe_data_size);
			execAndQuit(tmp_name);
		} else {
			// Wait for file to change, then run it, or if no change and we're now the lowest pid, restart
			int loopCount=0;
			printf("Self patching, but we're not the lowest PID, waiting for patching process to finish...\n");
			do {
				int len;
				U32 *data = fileAlloc(exe_name, &len);
				if (new_exe_data_size != len ||
					memcmp(new_exe_data, data, len)!=0)
				{
					// Not up to date yet
				} else {
					// Done being updated, restart!
					execAndQuit(exe_name);
				}
				fileFree(data);
				Sleep(2000);
				if (LowestPID())
				{
					loopCount++;
					if (loopCount > 10)
					{
						break; // It's been 20 seconds, .exe is not up to date, and I'm the lowest PID, take over
					}
				} else {
					loopCount = 0;
				}
			} while (true);
		}
	} while (true);
}
