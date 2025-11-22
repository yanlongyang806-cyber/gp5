#include "wininclude.h"
#include "net/net.h"
#include "timing.h"
#include "proclist.h"
#include "utils.h"
#include "sentry_comm.h"
#include "cpu_count.h"
#include "PerformanceCounter.h"
#include "timing.h"
#include "systemspecs.h"
#include "estring.h"
#include "string.h"
#include "autopatch.h"
#include "sysutil.h"
#include "process_util.h"
#include "MemoryMonitor.h"
#include "winutil.h"
#include "sock.h"
#include "cmdparse.h"
#include "gimmeDLLWrapper.h"
#include "zutils.h"
#include "file.h"
#include "logging.h"
#include "CrypticPorts.h"
#include "utilitiesLib.h"
#include "StringUtil.h"
#include "osDependent.h"
#include "crypt.h"
#include "fileutil2.h"
#include "earray.h"
#include "RegistryReader.h"
#include "UTF8.h"

//if true, then this is a "read only" sentry, and will only provide info, not do anything
bool gbReadOnly = false;


NetComm	*comm;
static NetLink	*server_link;

typedef struct
{
	S64		size;
	S64		unused;
	int		type;
} DiskInfo;

typedef struct
{
	char	name[64];
	F64		load;
	U64		ram;
	U64		ram_phys_used;
	U64		ram_virt_used;
	U64		ram_kernel_paged;
	U64		ram_kernel_nonpaged;

	U32		ether_send_sec;
	U32		ether_recv_sec;

	DiskInfo	disks[24];
	F64		disk_speed;
	F64		disk_queue;
} SentryInfo;

SentryInfo	sentry_info;

void sendString(Packet *pkt,char *label,char *value,U32 uid)
{
	pktSendU32(pkt,uid);
	pktSendString(pkt,label);
	pktSendString(pkt,value);
	pktSendF64(pkt,0);
}

void sendValue(Packet *pkt,char *label,F64 value,U32 uid)
{
	char	buf[1000],*s;

	if (!FINITE(value))
		value = 0;

	sprintf(buf,"%f",value);
	s = buf + strlen(buf)-1;
	while(s != buf && *s == '0')
		*s-- = 0;
	if (*s == '.')
		*s = 0;
	pktSendU32(pkt,uid);
	pktSendString(pkt,label);
	pktSendString(pkt,buf);
	pktSendF64(pkt,value);
}

char *shortCpuName(char *orig)
{
	char	*name=0,*args[20];
	int		i,count;

	orig = strdup(orig);
	if (strstri(orig,"Intel"))
		estrConcatf(&name,"Intel ");
	else if (strstri(orig,"AMD"))
		estrConcatf(&name,"Amd ");
	else
		estrConcatf(&name,"Unknown ");
	count = tokenize_line(orig,args,0);
	for(i=0;i<count;i++)
	{
		if (isdigit(args[i][0]))
			estrConcatf(&name,"%s.",args[i]);
	}
	while(!isdigit(name[strlen(name)-1]))
		name[strlen(name)-1] = 0;
	return name;
}

void getValues();

BOOL Is64BitWindows()
{
#if defined(_WIN64)
 return TRUE;  // 64-bit programs run only on Win64
#elif defined(_WIN32)
 // 32-bit programs run on both 32-bit and 64-bit Windows
 // so must sniff
 BOOL f64 = FALSE;
 return IsWow64Process(GetCurrentProcess(), &f64) && f64;
#endif
}


void sentryConnect(NetLink *link,void *user_data)
{
	Packet	*pkt = pktCreate(link,SENTRYCLIENT_CONNECT);
	char	buf[200];

	pktSendString(pkt,sentry_info.name);
	pktSend(&pkt);

	getValues();
	pkt = pktCreate(link,SENTRYCLIENT_STATS);

	sendString(pkt,"Machine",sentry_info.name,0);
	sendString(pkt,"Cpu_Name",shortCpuName(system_specs.cpuIdentifier),0);
	sendValue(pkt,"Cpu_Speed",system_specs.CPUSpeed,0);
	sendValue(pkt,"Cpu_Count",system_specs.numRealCPUs,0);
	sendValue(pkt,"Cpu_Load",sentry_info.load,0);

	sendValue(pkt,"RAM_Total",sentry_info.ram,0);

	sendString(pkt,"Video_Card",system_specs.videoCardName,0);
	sendString(pkt,"Video_Driver",system_specs.videoDriverVersion,0);
	sendValue(pkt,"Video_Memory",system_specs.videoMemory * (1 << 20),0);
	sendValue(pkt,"Video_Monitors",system_specs.numMonitors,0);

	sprintf(buf,"%d.%d",system_specs.servicePackMajor,system_specs.servicePackMinor);
	sendString(pkt,"OS_ServicePack",buf,0);
	sprintf(buf,"%d.%d",system_specs.highVersion,system_specs.lowVersion);
	sendString(pkt,"OS_Version",buf,0);
	sendValue(pkt,"OS_build",system_specs.build,0);
	sendValue(pkt,"OS_64",Is64BitWindows(),0);


	if (getDriverVersion(SAFESTR(buf), "Workspace Whiz/WorkspaceWhiz.dll" ))
		sendString(pkt,"Dll_WWhiz",buf,0);

	sendString(pkt,"Net_LocalIP",makeIpStr(getHostLocalIp()),0);
	sendString(pkt,"Net_PublicIP",makeIpStr(getHostPublicIp()),0);
	pktSend(&pkt);
}

void getPerfCounters() 
{ 
	static PerformanceCounter *counterNetwork=NULL;
	static PerformanceCounter *counterCPU=NULL;
	static PerformanceCounter *counterDisk=NULL;

	static int inited=0;
	if (!inited) { 
		inited = 1; 
		counterNetwork = performanceCounterCreate("Network Interface"); 
		if (counterNetwork) { 
			performanceCounterAdd(counterNetwork, "Bytes Sent/sec", &sentry_info.ether_send_sec); 
			performanceCounterAdd(counterNetwork, "Bytes Received/sec", &sentry_info.ether_recv_sec); 
		} 
		counterCPU = performanceCounterCreate("Processor"); 
		if (counterCPU) { 
			performanceCounterAddF64(counterCPU, "% Processor Time", &sentry_info.load); 
		}
		counterDisk = performanceCounterCreate("LogicalDisk"); 
		if (counterDisk) { 
			performanceCounterAddF64(counterDisk, "Avg. Disk Queue Length", &sentry_info.disk_queue); 
			performanceCounterAddF64(counterDisk, "Disk Bytes/sec", &sentry_info.disk_speed); 
		} 
	} 

	if (counterNetwork) 
		performanceCounterQuery(counterNetwork, NULL);
	if (counterCPU) 
		performanceCounterQuery(counterCPU, NULL);
	if (counterDisk) 
		performanceCounterQuery(counterDisk, NULL);
}

#include <psapi.h>
void getRamInfo()
{
	MEMORYSTATUSEX memoryStatus;
	ZeroMemory(&memoryStatus,sizeof(MEMORYSTATUSEX));
	memoryStatus.dwLength = sizeof(MEMORYSTATUSEX);

	GlobalMemoryStatusEx(&memoryStatus);
	sentry_info.ram				= memoryStatus.ullTotalPhys;
	sentry_info.ram_phys_used	= memoryStatus.ullTotalPhys - memoryStatus.ullAvailPhys;
	sentry_info.ram_virt_used	= memoryStatus.ullTotalPageFile - memoryStatus.ullAvailPageFile;



	{
		PERFORMANCE_INFORMATION perf_stats;

		perf_stats.cb = sizeof(perf_stats);
		GetPerformanceInfo(&perf_stats, sizeof(perf_stats));

		sentry_info.ram_kernel_paged = perf_stats.KernelPaged * perf_stats.PageSize;
		sentry_info.ram_kernel_nonpaged = perf_stats.KernelNonpaged * perf_stats.PageSize;
	}
}

void getFixedInfo()
{
	getRamInfo();
	strcpy(sentry_info.name,getHostName());
	systemSpecsInit();
}

void getDisk()
{
	int		i;
	ULARGE_INTEGER freeBytesAvailableToUser;
	ULARGE_INTEGER totalBytesOnDisk;
	char dir[CRYPTIC_MAX_PATH];

	for(i=0;i<24;i++)
	{
		DiskInfo	*disk = &sentry_info.disks[i];
		sprintf(dir,"%c:",i+'c');
		disk->type = GetDriveType_UTF8(dir);

		if (disk->type == DRIVE_FIXED && GetDiskFreeSpaceEx_UTF8(dir, &freeBytesAvailableToUser, &totalBytesOnDisk, NULL))
		{
			disk->size = totalBytesOnDisk.QuadPart;
			disk->unused = freeBytesAvailableToUser.QuadPart;
			//disk->type = GetDriveType(dir);
		}
	}
}

void getValues()
{
	static U32		load;

	getPerfCounters();
	getDisk();
	procGetList();
}

void sendValues()
{
	int		i;
	char	string[200];

	Packet	*pkt = pktCreate(server_link,SENTRYCLIENT_STATS);

	sendValue(pkt,"RAM_PhysUsed",sentry_info.ram_phys_used,0);
	sendValue(pkt,"RAM_VirtUsed",sentry_info.ram_virt_used,0);
	sendValue(pkt,"RAM_KernelPaged",sentry_info.ram_kernel_paged,0);
	sendValue(pkt,"RAM_KernelNonPaged",sentry_info.ram_kernel_nonpaged,0);

	sendValue(pkt,"NET_Sent",sentry_info.ether_send_sec,0);
	sendValue(pkt,"NET_Recv",sentry_info.ether_recv_sec,0);

	sendValue(pkt,"Cpu_Load",sentry_info.load,0);

	sendValue(pkt,"DiskPerf_BytesSec",sentry_info.disk_speed,0);
	sendValue(pkt,"DiskPerf_Queue",sentry_info.disk_queue,0);

	if (gbReadOnly)
	{
		sendValue(pkt, "ReadOnly_Local", 1.0f, 0);
	}
	else
	{
		sendValue(pkt, "ReadOnly_Local", 0.0f, 0);
	}


	for(i=0;i<ARRAY_SIZE(sentry_info.disks);i++)
	{
		DiskInfo	*disk = &sentry_info.disks[i];
		static char *type_names[] = { "Unknown", "NoRootDir", "Removable", "Fixed", "Remote", "CDRom", "RamDisk" };

		if (disk->size)
		{
			sprintf(string,"Disk_%C",i+'C');
			sendString(pkt,"Disk_Name",string,i);
			sendValue(pkt,"Disk_Size",disk->size,i);
			sendValue(pkt,"Disk_Free",disk->unused,i);
			sendValue(pkt,"Disk_PercFree",disk->unused * 100 / disk->size,i);
			if (disk->type >= 0 && disk->type < ARRAY_SIZE(type_names))
				sendString(pkt,"Disk_Type",type_names[disk->type],i);
		}
	}
	procSendList(pkt);

	pktSend(&pkt);

}

static int isProcessId(char *s)
{
	for(;*s;s++)
	{
		if (!isdigit(*s))
			return 0;
	}
	return 1;
}

void handleKill(Packet *pak)
{
	char	*process_name;

	process_name = pktGetStringTemp(pak);
	procGetList();
	if (isProcessId(process_name))
		kill(atoi(process_name));
	else
		procKillByName(process_name);
}

#define WORKINGDIR_COMMAND "WORKINGDIR("

//given a request to launch an x64 executable, if this is a 32-bit machine, just strips off the X64 part.
bool fixupX64Command(char *pCmd)
{
	if (IsUsingX64())
	{
		return false;
	}

	if (strStartsWith(pCmd, WORKINGDIR_COMMAND))
	{
		char *pFirstRightParens = strchr(pCmd, ')');
		char *pFirstSpace;
		char *pX64;

		if (!pFirstRightParens)
		{
			return false;
		}

		pCmd = pFirstRightParens + 1;

		while (IS_WHITESPACE(*pCmd))
		{
			pCmd++;
		}
		
		pX64 = strstri(pCmd, "X64");
		pFirstSpace = strchr(pCmd, ' ');

		if (pX64 && (!pFirstSpace || pX64 < pFirstSpace))
		{
			size_t iLen = strlen(pCmd);
			memmove(pX64, pX64 + 3, iLen + 1 - (pX64 - pCmd));

			return true;
		}
	}

	return false;
}

void handleLaunch(Packet *pak, bool bAndWait)
{
	char	*cmd;
	int		ret=0;

	cmd = pktGetStringTemp(pak);

	printf("Got launch%s command \"%s\" from %s\n", bAndWait ? "AndWait" : "", cmd, makeIpStr(linkGetIp(pktLink(pak))));
	filelog_printf("sentrycmd.log","Got launch%s command \"%s\" from %s\n", bAndWait ? "AndWait" : "", cmd, makeIpStr(linkGetIp(pktLink(pak))));

	if (fixupX64Command(cmd))
	{
		filelog_printf("sentrycmd.log", "This is a 32 bit machine, fixed up cmd to: %s",
			cmd);
	}


	if (strStartsWith(cmd, WORKINGDIR_COMMAND))
	{
		char workingDir[MAX_PATH];
		char tempFile[MAX_PATH];
		char *pFirstRightParens = strchr(cmd, ')');

		if (pFirstRightParens)
		{
			char *pCommandBegin = pFirstRightParens + 1;

			while (*pCommandBegin == ' ')
			{
				pCommandBegin++;
			}

			*pFirstRightParens = 0;
			strcpy(workingDir, cmd + strlen(WORKINGDIR_COMMAND));

			sprintf(tempFile, "%s\\fake.txt", workingDir);
			mkdirtree(tempFile);
			
			printf("Executing command \"%s\" in working dir \"%s\"\n", 
				pCommandBegin, workingDir);
			if (bAndWait)
			{
				if (system_w_timeout(pCommandBegin, workingDir, 15) == -1)
				{
					filelog_printf("sentrycmd.log","Command timed out!");
				}
			}
			else
			{
				system_w_workingDir(pCommandBegin, workingDir);
			}
		}
		else
		{
			printf("Badly formatted WORKINGDIR(\n");
		}
	}
	else
	{
		printf("No working dir... executing\n");
		if (bAndWait)
		{
			if (system_w_timeout(cmd, NULL, 15) == -1)
			{
				filelog_printf("sentrycmd.log","Command timed out!");
			}
		}
		else
		{
			system_detach(cmd, 1, 0);
		}
	}
}

void handleCreateFile(Packet *pak)
{
	char *pFileNameToCreate = pktGetStringTemp(pak);
	int iCompressedBufferSize = pktGetBits(pak, 32);
	int iUncompressedBufferSize = pktGetBits(pak, 32);
	void *pCompressedBuffer = calloc(iCompressedBufferSize, 1);
	void *pUncompressedBuffer = calloc(iUncompressedBufferSize, 1);

	pktGetBytes(pak, iCompressedBufferSize, pCompressedBuffer);

	unzipData(pUncompressedBuffer, &iUncompressedBufferSize, pCompressedBuffer, iCompressedBufferSize);

	if (iUncompressedBufferSize)
	{
		FILE *pOutFile;
		mkdirtree(pFileNameToCreate);

		pOutFile = fopen(pFileNameToCreate, "wb");

		if (pOutFile)
		{
			fwrite(pUncompressedBuffer, iUncompressedBufferSize, 1, pOutFile);

			fclose(pOutFile);
		}
	}

	free(pCompressedBuffer);
	free(pUncompressedBuffer);
}


void handleGetFileCRC(Packet *pak,NetLink* link)
{
	int iRequestID = pktGetBits(pak, 32);
	char *pFileName = pktGetStringTemp(pak);
	int iMonitorLinkID = pktGetBits(pak, 32);
	Packet *pOutPack;

	U32 iCRC = 0;

	if (fileExists(pFileName))
	{
		iCRC = cryptAdlerFile(pFileName);
	}

	pOutPack = pktCreate(link, SENTRYCLIENT_HEREISFILECRC);
	pktSendBits(pOutPack, 32, iRequestID);
	pktSendBits(pOutPack, 32, iMonitorLinkID);
	pktSendBits(pOutPack, 32, iCRC);
	pktSend(&pOutPack);
}


void handleGetFileContents(Packet *pak,NetLink* link)
{
	int iRequestID = pktGetBits(pak, 32);
	char *pFileName = pktGetStringTemp(pak);
	int iMonitorLinkID = pktGetBits(pak, 32);
	Packet *pOutPack;

	void *pContents;
	int iSize = 0;

	pContents = fileAlloc(pFileName, &iSize);

	pOutPack = pktCreate(link, SENTRYCLIENT_HEREISFILECONTENTS);
	pktSendBits(pOutPack, 32, iRequestID);
	pktSendBits(pOutPack, 32, iMonitorLinkID);
	pktSendString(pOutPack, pFileName);
	pktSendBits(pOutPack, 32, iSize);
	if (iSize)
	{
		pktSendBytes(pOutPack, iSize, pContents);
	}
	pktSend(&pOutPack);

	free(pContents);
}

void handleGetDirectoryContents(Packet *pak, NetLink *link)
{
	int iRequestID = pktGetBits(pak, 32);
	char *pDirName = pktGetStringTemp(pak);
	int iMonitorLinkID = pktGetBits(pak, 32);
	Packet *pOutPack;
	char *pOutString = NULL;
	bool bNoRecurse = false;

	if (strStartsWith(pDirName, GETDIRCONTENTS_PREFIX_NORECURSE))
	{
		bNoRecurse = true;
		pDirName += strlen(GETDIRCONTENTS_PREFIX_NORECURSE);
	}


	if (dirExists(pDirName))
	{
		char **ppFiles = bNoRecurse ? fileScanDirFoldersNoSubdirRecurse(pDirName, FSF_FILES) : fileScanDir(pDirName);
		FOR_EACH_IN_EARRAY_FORWARDS(ppFiles, char, pFile)
		{
			estrConcatf(&pOutString, "%s%s", pOutString ? ";" : "", pFile);
		}
		FOR_EACH_END;

		fileScanDirFreeNames(ppFiles);
	}

	pOutPack = pktCreate(link, SENTRYCLIENT_HEREAREDIRECTORYCONTENTS);
	pktSendBits(pOutPack, 32, iRequestID);
	pktSendBits(pOutPack, 32, iMonitorLinkID);
	pktSendString(pOutPack, pOutString);
	pktSend(&pOutPack);

	estrDestroy(&pOutString);
}





void sentryServerMsg(Packet *pak,int cmd,NetLink* link,void *user_data)
{

	//hopefully goes without saying that gbReadOnly MUST be respected here
	switch(cmd)
	{
		xcase SENTRYSERVER_AUTOPATCH:
			autopatchHandleMessage(pak,cmd,link,user_data);
		xcase SENTRYSERVER_LAUNCH:
			if (!gbReadOnly)
			{
				handleLaunch(pak, false);
			}
		xcase SENTRYSERVER_LAUNCH_AND_WAIT:
			if (!gbReadOnly)
			{
				handleLaunch(pak, true);
			}
		xcase SENTRYSERVER_KILL:
			if (!gbReadOnly)
			{
				handleKill(pak);
			}
		xcase SENTRYSERVER_DUPQUIT:
			filelog_printf("quitlog.log","SENTRYSERVER_DUPQUIT");
			logWaitForQueueToEmpty();
			exit(0);
		xcase SENTRYSERVER_CREATEFILE:
			if (!gbReadOnly)
			{
				handleCreateFile(pak);
			}
		xcase SENTRYSERVER_GETFILECRC:
			handleGetFileCRC(pak, link);
		xcase SENTRYSERVER_GETFILECONTENTS:
			handleGetFileContents(pak, link);
		xcase SENTRYSERVER_GETDIRECTORYCONTENTS:
			handleGetDirectoryContents(pak, link);
		xcase SENTRYSERVER_AUTOPATCH_X64FILES:
			autopatchHandleMessage64(pak,cmd,link,user_data);
	}
}

static U32 siHideConsoleTime = 0;

void DisplayTemporaryWarning(char *pStr)
{
	char *pSystemString = NULL;
	estrPrintf(&pSystemString, "messagebox -title \"Sentry Notification\" -message \"%s\" -lifespan 300",
		pStr);
	system_detach(pSystemString, 1, 1);
}

void CheckForReadOnly(void)
{
	RegReader rr;
	int value = 0;

	if (fileExists(".//SentryReadOnly.txt"))
	{
		gbReadOnly = true;
	}


	rr = createRegReader();
	initRegReader(rr, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE");
	if (rrReadInt(rr, "SentryReadOnly", &value))
	{
		if (gbReadOnly)
		{
			//do nothing
		}
		else
		{
			rrDelete(rr, "SentryReadOnly");

			DisplayTemporaryWarning("Sentry.exe  was previously running in locally-enforced read-only mode, and no longer is (because SentryReadOnly.txt does not exist) .... if this was not intentional, please remedy the situation");
		}
	}
	else
	{
		if (gbReadOnly)
		{
			DisplayTemporaryWarning("Sentry.exe was is now running in locally-enforced read-only mode, because SentryReadOnly.txt exists. If this was not intentional, please remedy the situation");

			rrWriteInt(rr, "SentryReadOnly", 1);
		}
	}

	rrClose(rr);
	destroyRegReader(rr);

}



int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, WCHAR*    pWideCmdLine, int nCmdShow)
{
	int		seconds = timerAlloc(), not_connected = timerAlloc();
	int		i,argc,nopatch=0;
	char	*args[10],*servername = "sentryserver",*cmdLine;
	NetListen	*monitor=0;

	gbCavemanMode = 1;
	EXCEPTION_HANDLER_BEGIN
	char *lpCmdLine = UTF16_to_UTF8_CommandLine(pWideCmdLine);	
	DO_AUTO_RUNS

	gimmeDLLDisable(1);
	cmdLine = strdup(lpCmdLine);
	argc = tokenize_line(cmdLine,args,0);

	//cmdParseCommandLine(argc,args);
	comm = commCreate(20,0);
	for(i=0;i<argc;i++)
	{
		if (stricmp(args[i],"-console")==0)
		{
			newConsoleWindow();
		}
		if (stricmp(args[i],"-pause")==0)
		{
			for(;;)
				Sleep(1);
		}
		if (stricmp(args[i],"-monitor")==0)
		{
			extern void monitorExample();

			newConsoleWindow();
			monitorExample();
			return 0;
		}
		if (stricmp(args[i],"-launch")==0)
		{
			NetLink	*relay;
			char	*cmd;
			Packet	*pak;

			servername = args[i+1];
			cmd = args[i+2];
			relay = commConnectWait(comm,LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,servername,SENTRYMONITOR_PORT,0,0,0,0,5);
			if (!relay)
			{
				newConsoleWindow();
				printf("can't connect to relay server on %s\n",servername);
			}
			pak = pktCreate(relay,SENTRYSERVER_LAUNCH);
			pktSendString(pak,cmd);
			pktSend(&pak);
			Sleep(1);
			return 0;
		}
		if (stricmp(args[i],"-pdh")==0)
		{
			void pdhBrowse();

			newConsoleWindow();
			pdhBrowse();
			return 0;
		}
		if (stricmp(args[i],"-nopatch")==0)
			nopatch = 1;
		if (stricmp(args[i],"-server")==0)
			servername = args[i+1];
	}
	getFixedInfo();

	CheckForReadOnly();


	for(;;)
	{
		if (linkDisconnected(server_link) || (!linkConnected(server_link) && timerElapsed(not_connected) > 60.0))
		{
			linkRemove(&server_link);
			autopatchReset();
			if (timerElapsed(not_connected) > 60.0)
			{
				timerStart(not_connected);
				ShellExecute( NULL, L"open", L"ipconfig.exe", L"/flushdns", L"", SW_HIDE );
				ShellExecute( NULL, L"open", L"ipconfig.exe", L"/registerdns", L"", SW_HIDE );
			}
		}
		if (!server_link)
			server_link = commConnect(comm,LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,servername,SENTRYSERVER_PORT,sentryServerMsg,sentryConnect,0,0);
		commMonitor(comm);
		if (linkConnected(server_link))
		{
			timerStart(not_connected);
			if (nopatch || autopatchOk(server_link))
			{
				if (!monitor)
				{
					timerStart(not_connected);
					if (!monitor) // see if we can grab the local listen port. keep trying if we fail
						monitor = commListen(comm,LINKTYPE_UNSPEC, 0,SENTRYMONITOR_PORT,sentryServerMsg,0,0,0);
				}
				if (timerElapsed(seconds) >= 1)
				{
					timerStart(seconds);
					getValues();
					getRamInfo();
					sendValues();

					if (siHideConsoleTime && siHideConsoleTime < timeSecondsSince2000())
					{
						hideConsoleWindow();
					}
				}
			}
		}
	}
	EXCEPTION_HANDLER_END
}

