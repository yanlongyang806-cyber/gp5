#include "wininclude.h"
#include "network/crypt.h"
#include "net/net.h"
#include "net/netpacketutil.h"
#include "file.h"
#include "mathutil.h"
#include "assert.h"
#include "sysutil.h"
#include "utils.h"
#include "windows.h"
#include "autopatch.h"
#include "error.h"
#include "logging.h"
#include "sentry_comm.h"
#include "windefinclude.h"
#include "osdependent.h"
#include "UTF8.h"

#pragma warning (disable:6335)

char *Force32BitFileName(char *pInName)
{
	static char *spRetVal = NULL;
	estrCopy2(&spRetVal, pInName);
	estrReplaceOccurrences(&spRetVal, "X64.", ".");

	return spRetVal;
}

char *Force64BitFileName(char *pInName)
{
	static char *spRetVal = NULL;
	estrCopy2(&spRetVal, pInName);

	//lazy way to avoid ending up with X64X64.exe
	estrReplaceOccurrences(&spRetVal, "X64.", ".");
	estrReplaceOccurrences(&spRetVal, ".", "X64.");

	return spRetVal;
}


typedef struct
{
	S64		size;
	U32		values[4];
} Checksum;


static void checksumFile(char *fname,Checksum *check,int update_stats)
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

void safeRenameFile(char *oldnamep,char *newnamep)
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
	if (!fileExists(newname))
	{
		goto fail;
	}
	chmod(newname,_S_IREAD | _S_IWRITE);
	if (unlink(newname) != 0)
	{
		if (strEndsWith(newname,".exe"))
		{
			char	*s,new_exename[MAX_PATH];

			strcpy(new_exename,newname);
			s = strrchr(new_exename,'.');
			strcpy_s(s,5,".old");
			safeRenameFile(newname,new_exename);
		}
		else
			FatalErrorf("ErrRenameDelete %s",newname);
	}
	for(i=0;i<100;i++)
	{
		if (rename(oldname,newname) == 0)
			return;
		Sleep(100);
	}
	fail:
	FatalErrorf("ErrRenameFailed %s %s",oldname,newname);
}

void writeFile(char *fname,char *how,void *mem,U32 len)
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
	filelog_printf("quitlog.log","execAndQuit result %d",result);
	logWaitForQueueToEmpty();
	exit(0);
}

static	U8	new_name[MAX_PATH],tmp_name[MAX_PATH];

static int checksum_sent,version_ok;

void autopatchReset()
{
	version_ok = checksum_sent = 0;
}

void safeUnlink(char *fname)
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

//returns true if it wants to restart, but 64 bit exe doesn't seem to exist
bool MaybeRestartIn64BitMode(void)
{
	int iPointerSize = sizeof(void*);
	char *pMyName;
	char *pMyName64 = NULL;
	char *pMyNewName64 = NULL;
	U32 iMyTime, iMyTime64;
	char *p64BitCommandLine = NULL;

	//we're already a 64 bit exe
	if (iPointerSize == 8)
	{
		return false;
	}

	//this is a 32-bit machine
	if (!IsUsingX64())
	{
		return false;
	}


	pMyName = getExecutableName();
	estrCopy2(&pMyName64, pMyName);

	estrReplaceOccurrences(&pMyName64, ".exe", "X64.exe");

	estrCopy2(&pMyNewName64, pMyName64);
	estrReplaceOccurrences(&pMyNewName64, ".exe", ".new");

	backSlashes(pMyNewName64);
	backSlashes(pMyName64);


	//at this point, it's fairly likely that we have just done an autopatch, so that X64.new is the newest version... so we need
	//to keep track of both of those possible filenames


	//neither .exe nor .new exists of X64, so we need to force a re-patch
	if (!fileExists(pMyName64) && !fileExists(pMyNewName64))
	{
		return true;
	}

	if (fileExists(pMyName64) && fileExists(pMyNewName64))
	{
		//both .exe and .new exist for x64 version. delete .exe and rename .new

		safeUnlink(pMyName64);
		safeRenameFile(pMyNewName64, pMyName64);
	}
	else if (fileExists(pMyNewName64))
	{
		//only .new exists, rename it
		safeRenameFile(pMyNewName64, pMyName64);
	}
	else
	{
		//only .exe exists, we're happy
	}


	iMyTime = fileLastChangedSS2000(pMyName);
	iMyTime64 = fileLastChangedSS2000(pMyName64);

	if (!iMyTime64 || !iMyTime)
	{
		assertmsgf(0, "Unable to get file times from %s or %s",
			pMyName, pMyName64);	
	}

	if (iMyTime64 < iMyTime - 2 * 60 * 60)
	{
		//64 bit version is more than 2 hours older than normal version, something funny happened with patching
		return true;
	}

	estrPrintf(&p64BitCommandLine, "%s %s", pMyName64, GetCommandLineWithoutExecutable());
		
	estrTrimLeadingAndTrailingWhitespace(&p64BitCommandLine);

	if (system_detach_with_fulldebug_fixup(p64BitCommandLine, false, false))
	{
		exit(0);
	}	
	else
	{
		assertmsgf(0, "Unable to restart sentry in 64 bit mode even though everything seems to check out. Cmdline: %s", 
			p64BitCommandLine);
	}

	estrDestroy(&pMyName64);


	return true;
}

int autopatchOk(NetLink *link)
{
	Packet		*pak;
	U8			exe_name[MAX_PATH],old_name[MAX_PATH];
	Checksum	checksum;
	bool bWantToRestartButCant = false;

	if (checksum_sent)
		return version_ok;
	strcpy(exe_name,getExecutableName());
	backSlashes(exe_name);
	strcpy(new_name,exe_name);
	strcpy(tmp_name,exe_name);
	strcpy(old_name,exe_name);
	strcpy_s(new_name + strlen(new_name)-3,4,"new");
	strcpy_s(tmp_name + strlen(tmp_name)-3,4,"tmp");
	strcpy_s(old_name + strlen(old_name)-3,4,"old");
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

	//if we WANT to restart in 64 bit mode but can't, then either something is greviously wrong, or we're right in the midst of
	//the 32-bit-to-64-bit sentry changeover, in which case we force another autopatch, by reporting our own file size as 1
	bWantToRestartButCant = MaybeRestartIn64BitMode();

	filelog_printf("autopatch","requested patch check");
	pak = pktCreate(link,SENTRYCLIENT_AUTOPATCH);
	pktSendU32(pak,SENTRY_PROTOCOL_VERSION);
	checksumFile(exe_name,&checksum,0);
	pktSendU32(pak, bWantToRestartButCant ? 1 : checksum.size);
	pktSendBytes(pak,sizeof(checksum.values),&checksum.values);
	pktSend(&pak);
	checksum_sent = 1;
	return 0;
}

void autopatchHandleMessage(Packet *pak,int cmd,NetLink* link,void *user_data)
{
	int	response;
	U8	*mem;
	U32	size;

	response = pktGetU32(pak);
	filelog_printf("autopatch","received response %d",response);
	if (response == 1)
	{
		Packet *pak_send = pktCreate(link,SENTRYCLIENT_AUTOPATCH_DONE);
		pktSend(&pak_send);
		version_ok = 1;

		return;
	}
	if (response == 0)
	{
		Errorf("error from server: %s\n",pktGetStringTemp(pak));
	}
	assert(response == 2);
	mem = pktGetZipped(pak,&size);
	if (!size)
		FatalErrorf("ErrZeroSizeClient");
	filelog_printf("autopatch","received %d bytes",size);
	writeFile(Force32BitFileName(new_name),"!wb",mem,size);
	writeFile(Force32BitFileName(tmp_name),"!wb",mem,size);
	execAndQuit(Force32BitFileName(tmp_name));
}

void autopatchHandleMessage64(Packet *pak,int cmd,NetLink* link,void *user_data)
{
	U8	*mem;
	U32	size;


	filelog_printf("autopatch","received 64 bit autopatch");

	mem = pktGetZipped(pak,&size);
	if (!size)
		FatalErrorf("ErrZeroSizeClient");
	filelog_printf("autopatch","received %d bytes",size);
	writeFile(Force64BitFileName(new_name),"!wb",mem,size);
	writeFile(Force64BitFileName(tmp_name),"!wb",mem,size);

}



