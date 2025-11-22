#include <stdio.h>
#include "wininclude.h"
#include <tlhelp32.h>
#include "stdtypes.h"
#include <psapi.h>
#include "proclist.h"
#include "net/net.h"
#include "assert.h"
#include "utils.h"
#include "earray.h"
#include "file.h"
#include "error.h"
#include "StashTable.h"
#include "process_util.h"
#include "sentry_comm.h"
#include "UTF8.h"

ProcessList			process_list;
static U32			total_mem;

static void addFileTime(FILETIME *a,FILETIME *b,FILETIME *c)
{
	LARGE_INTEGER lia, lib, lic;
	lia.LowPart = a->dwLowDateTime;
	lia.HighPart = a->dwHighDateTime;
	lib.LowPart = b->dwLowDateTime;
	lib.HighPart = b->dwHighDateTime;
	lic.QuadPart = lia.QuadPart + lib.QuadPart;
	c->dwHighDateTime = lic.HighPart;
	c->dwLowDateTime = lic.LowPart;
}

static U32 milliSecondsRunning(FILETIME kt,FILETIME ut)
{
FILETIME	tt;
double		ftime,seconds;
U32			t,ts;
#define SEC_SCALE 0.0000001

	addFileTime(&kt,&ut,&tt);
	seconds = tt.dwLowDateTime * SEC_SCALE;
	ftime = tt.dwHighDateTime * SEC_SCALE * 4294967296.0;
	t = ftime * 1000;
	ts = seconds * 1000;
	return t + ts;
}

static void getProcessName(HANDLE p, char *pOutName, int iOutNameSize, char *pOutPath, int iOutPathSize)
{
#if 1
	char *pExeFilePath = NULL;
	char	*exe_name, *s;

	estrStackCreate(&pExeFilePath);

	if (!GetProcessImageFileName_UTF8(p,&pExeFilePath))
	{
		estrCopy2(&pExeFilePath, "UNKNOWN");
	}

	exe_name = strrchr(pExeFilePath,'\\');
	if (!exe_name++)
		exe_name = pExeFilePath;
	if (s = strrchr(exe_name, '.'))
		*s = 0;
	strcpy_s(pOutName,iOutNameSize,exe_name);
	strcpy_s(pOutPath,iOutPathSize,pExeFilePath);

	estrDestroy(&pExeFilePath);

#else
	DWORD			dwSize2;
	HMODULE			hMod[1000] ;
	char			szExeFilePath[1000],*exe_name = "total", *s;

	if(!EnumProcessModules(p, hMod, sizeof( hMod ), &dwSize2 ) )
	{
		strcpy_s(str,strSize, "UNKNOWN");
		return;
	}

	szExeFilePath[0] = 0;
	if (0==GetModuleFileNameEx( p, hMod[0],szExeFilePath, sizeof( szExeFilePath ) )) {
		szExeFilePath[0] = 0;
	}
	exe_name = strrchr(szExeFilePath,'\\');
	if (!exe_name++)
		exe_name = szExeFilePath;
	if (s = strrchr(exe_name, '.'))
		*s = 0;
	strcpy_s(str,strSize,exe_name);
#endif
}

int procInfoSend(ProcessInfo *pi,Packet *pak,int num_processors_scale)
{
	F32		cpu_elapsed,clock_elapsed,cpu_usage,cpu_usage60;
	U32		msecs;
	int		end,pid;
	void sendString(Packet *pkt,char *label,char *value,U32 uid);
	void sendValue(Packet *pkt,char *label,F64 value,U32 uid);

	msecs = pi->time_tables[0];
	cpu_elapsed = pi->time_tables[0] - pi->time_tables[1];
	clock_elapsed = process_list.timestamp_tables[0] - process_list.timestamp_tables[1];
	if (process_list.timestamp_tables[1]) {
		cpu_usage = cpu_elapsed / clock_elapsed;
	} else {
		// initial case
		cpu_usage = 0;
	}

	for (end=1; end < NUM_TICKS-1 && process_list.timestamp_tables[end] && pi->time_tables[end]; end++);
	end--;
	cpu_elapsed = pi->time_tables[0] - pi->time_tables[end];
	clock_elapsed = process_list.timestamp_tables[0] - process_list.timestamp_tables[end];
	if (clock_elapsed) {
		cpu_usage60 = cpu_elapsed / clock_elapsed;
	} else {
		cpu_usage60 = 0;
	}

	pid = pi->process_id;
	sendString(pak,"Process_Name",pi->exename,pid);
	sendString(pak,"Process_Path",pi->exepath,pid);
	sendString(pak,"Process_Title",pi->title,pid);
	sendValue(pak,"Process_PID",pi->process_id,pid);
	sendValue(pak,"Process_PhysMem",pi->mem_used_phys,pid);
	sendValue(pak,"Process_VirtMem",pi->mem_used_virt,pid);
	sendValue(pak,"Process_Usage",100 * cpu_usage/num_processors_scale,pid);
	sendValue(pak,"Process_Usage60",100 * cpu_usage60/num_processors_scale,pid);

	return 1;
}

static void calcTimers(ProcessInfo *pi,U32 msecs)
{
int		j,d;

	if (!pi->count)
	{
		for(j=0;j<NUM_TICKS;j++)
			pi->time_tables[j] = msecs;
	} else {
		memmove(&pi->time_tables[1],&pi->time_tables[0],(NUM_TICKS-1) * sizeof(U32));
		d = pi->time_tables[0] - msecs;
		if (d > 0)
		{
			// this will (probably) help when 60 days worth of CPU time (2^32 millis) has passed
			for(j=1;j<NUM_TICKS;j++)
				pi->time_tables[j] -= d;
		}
		pi->time_tables[0] = msecs;
	}
}

void showProc(ProcessInfo *pi)
{
	printf("%s [%"FORM_LL"d]\n",pi->exename,pi->mem_used_virt);
}

void procSendList(Packet *pak)
{
	int			i;

	for(i=0;i<eaSize(&process_list.processes);i++)
	{
		procInfoSend(process_list.processes[i],pak,1);
	}
}

static BOOL CALLBACK listWindowsCB(HWND hwnd, LPARAM lParam)
{
	DWORD	processID;
	int		i;
	WINDOWINFO pwi;
	char *pTitle = NULL;

	if(!GetWindowThreadProcessId(hwnd, &processID))
		return 1;
	if (!GetWindowInfo(hwnd,&pwi))
		return 1;
	if (!(pwi.dwStyle & WS_VISIBLE))
		return 1;
	estrStackCreate(&pTitle);

	if(!GetWindowText_UTF8(hwnd, &pTitle))
	{
		estrDestroy(&pTitle);
		return TRUE;
	}

	for(i=0;i<eaSize(&process_list.processes);i++)
	{
		if (process_list.processes[i]->process_id == processID)
		{
			char	*str = process_list.processes[i]->title;
			int		len = (int)strlen(str);
			snprintf_s(str+len,MAX_TITLE-len,"%s%s",str[0] ? "<BR>" : "",pTitle);
		}
	}

	estrDestroy(&pTitle);
	return TRUE;
}

void procGetList()
{
	PROCESSENTRY32 pe;
	BOOL retval;
	HANDLE hSnapshot;
	U32		total_mem_phys=0, total_mem_virt=0;
	ProcessInfo	*pi;
	int			i,launcher_count=0;
	FILETIME	total_time = {0,0},zero_time = {0,0};
	PROCESS_MEMORY_COUNTERS mem_counters;
	FILETIME	current_time;
	SYSTEMTIME	current_system_time;
	U32			current_time_millis;
	int			reset_stats=0;

	GetSystemTime(&current_system_time); // gets current time
	SystemTimeToFileTime(&current_system_time, &current_time);  // converts to file time format
	current_time_millis = milliSecondsRunning(current_time, zero_time);

	hSnapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
	if (hSnapshot==INVALID_HANDLE_VALUE)
	{
		printf("cannot create snapshot\n");
		return;
	}

	pe.dwSize=sizeof(PROCESSENTRY32);
	retval=Process32First(hSnapshot,&pe);
	while(retval)
	{
		retval=Process32Next(hSnapshot,&pe);
		for(i=0;i<eaSize(&process_list.processes);i++)
		{
			if (process_list.processes[i]->process_id == pe.th32ProcessID)
			{
				if (process_list.processes[i]->tag)
					process_list.processes[i]->count++;
				process_list.processes[i]->tag = 0;
				break;
			}
		}
		if (i >= eaSize(&process_list.processes))
		{
			pi = calloc(sizeof(ProcessInfo),1);
			pi->process_id = pe.th32ProcessID;
			eaPush(&process_list.processes,pi);
		}
	}
	CloseHandle(hSnapshot);

	for(i=eaSize(&process_list.processes)-1;i>=0;i--)
	{
		pi = process_list.processes[i];
		if (pi->tag)
		{
			// Add to process_list.total_offset!
			process_list.total_offset += pi->time_tables[0];
			free(pi);
			eaRemove(&process_list.processes,i);
			continue;
		}
		else
		{
			HANDLE		p;
			U32			msecs;
			FILETIME	cr,ex,kt,ut;

			pi->tag = 1;
			p = OpenProcess(PROCESS_QUERY_INFORMATION| PROCESS_VM_READ,FALSE,pi->process_id);
			if (!p) {
				// Unable to query the process, reset stats, otherwise they'll be very messed up when this process comes back!
				//Apparently on some systems we get processes we can *never* query, so let's not reset the stats every tick,
				// and just deal with the wacky values when we get them =(
				//This also happens if we get a list of process IDs, and the process is
				// closed before we can query it, treat it as closed now then!  Otherwise
				// the DbServer will assume it's closed because it was not reported upon.
				//reset_stats = 1;
				// Treat as closed for this tick
				process_list.total_offset += pi->time_tables[0];
				free(pi);
				eaRemove(&process_list.processes,i);
				continue;
			}
			GetProcessTimes(p,&cr,&ex,&kt,&ut);
			if (GetProcessMemoryInfo(p,&mem_counters,sizeof(mem_counters)))
			{
				pi->mem_used_phys = mem_counters.WorkingSetSize;
				total_mem_phys += pi->mem_used_phys;
				pi->mem_used_virt = mem_counters.PagefileUsage;
				total_mem_virt += pi->mem_used_virt;
			}
			getProcessName(p,SAFESTR(pi->exename), SAFESTR(pi->exepath));
			CloseHandle(p);
			

			msecs = milliSecondsRunning(kt,ut);
			addFileTime(&kt,&total_time,&total_time);
			addFileTime(&ut,&total_time,&total_time);

			calcTimers(pi,msecs);
			//if (!pi->crashed && (pi->crashed = isProcessCrashed(pi->process_id))) {
			//	notifyProcessCrashed(pi->process_id, pi->container_id, pi->container_type, pi->crashed==2);
			//}
		}

	}

	if (0) {
		process_list.total.mem_used_phys = total_mem_phys;
		process_list.total.mem_used_virt = total_mem_virt;
	} else {
		MEMORYSTATUSEX memoryStatus;
		ZeroMemory(&memoryStatus,sizeof(MEMORYSTATUSEX));
		memoryStatus.dwLength = sizeof(MEMORYSTATUSEX);

		GlobalMemoryStatusEx(&memoryStatus);
		process_list.total.mem_used_phys = (memoryStatus.ullTotalPhys - memoryStatus.ullAvailPhys) >> 10;
		process_list.total.mem_used_virt = (memoryStatus.ullTotalPageFile - memoryStatus.ullAvailPageFile) >> 10;
	}

	// Update timestamp_tables
	if (process_list.total.count==0) {
		memset(process_list.timestamp_tables, 0, sizeof(process_list.timestamp_tables));
	}
	memmove(&process_list.timestamp_tables[1], &process_list.timestamp_tables[0], (NUM_TICKS-1)*sizeof(U32));
	process_list.timestamp_tables[0] = current_time_millis;
	calcTimers(&process_list.total,milliSecondsRunning(total_time,zero_time) + process_list.total_offset);
	process_list.total.count++;

	if (reset_stats) {
		process_list.total.count = 0;
	}
	for(i=0;i<eaSize(&process_list.processes);i++)
		process_list.processes[i]->title[0] = 0;
	EnumWindows(listWindowsCB, (LPARAM)NULL);
#if 0
	printf("\n\n\n");
	for(i=0;i<process_list.count;i++)
		showProc(&process_list.processes[i]);
#endif
}

void procKillByName(char *process_name)
{
	int			i;
	ProcessInfo	*pi;
	char		*s,name[256];

	strcpy(name,process_name);
	s = strrchr(name,'.');
	if (s)
		*s = 0;
	for(i=0;i<eaSize(&process_list.processes);i++)
	{
		pi = process_list.processes[i];

		if (stricmp(pi->exename,name)==0)
			kill(pi->process_id);		
	}
}
