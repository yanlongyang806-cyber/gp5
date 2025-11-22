// BatUtil.cpp : Defines the entry point for the console application.
//
//#define _CRT_SECURE_NO_DEPRECATE 1
//#define CRT_SECURE_NO_DEPRECATE 1

#include "stdafx.h"
#include <stdio.h>
#include <time.h>
#include <Windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <sys/stat.h>
#pragma comment (lib, "winmm.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "psapi.lib")


BOOL ProcessNameMatch( DWORD processID , WCHAR * targetName)
{
	WCHAR szProcessName[MAX_PATH] = L"unknown";

	// Get a handle to the process.

	HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION |
		PROCESS_VM_READ,
		FALSE, processID );

	// Get the process name.

	if (NULL != hProcess )
	{
		HMODULE hMod;
		DWORD cbNeeded;

		if ( EnumProcessModules( hProcess, &hMod, sizeof(hMod), 
			&cbNeeded) )
		{
			GetModuleBaseName( hProcess, hMod, szProcessName, 
				sizeof(szProcessName) );
		}
		else {
			CloseHandle( hProcess );
			return FALSE;
		}
	}
	else return FALSE;

	// Print the process name and identifier.
	CloseHandle( hProcess );

	if (!wcschr(targetName, L'.')) {
		WCHAR *s = wcsrchr(szProcessName, L'.');
		if (s)
			*s = 0;
	}
	if (_wcsicmp(szProcessName, targetName)==0)
		return TRUE;
	else
		return FALSE;
}

int ProcessCount(WCHAR * procName)
{
	// Get the list of process identifiers.
	DWORD aProcesses[1024], cbNeeded, cProcesses;
	unsigned int i;
	int count = 0;

	if ( !EnumProcesses( aProcesses, sizeof(aProcesses), &cbNeeded ) )
		return 0;

	// Calculate how many process identifiers were returned.

	cProcesses = cbNeeded / sizeof(DWORD);

	// Print the name and process identifier for each process.

	for ( i = 0; i < cProcesses; i++ )
	{
		if(ProcessNameMatch( aProcesses[i] , procName))
			count++;
	}
	return count;
}

// Much slower, but finds 64-bit processes
int ProcessCount64(WCHAR *procName)
{
	PROCESSENTRY32		pe32 = {0};
	HANDLE				hProcessSnap;
	int					notDone;
	int					count = 0;

	// Find all the processes in the system.

	hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	if(hProcessSnap == INVALID_HANDLE_VALUE)
	{
		// This should never happen, but you never know.

		return 0;
	}

	pe32.dwSize = sizeof(pe32);

	for(notDone = Process32First(hProcessSnap, &pe32); notDone; notDone = Process32Next(hProcessSnap, &pe32))
	{
		WCHAR szProcessName[MAX_PATH] = L"unknown";
		//printf("   %s\n", pe32.szExeFile);
		wcscpy(szProcessName, pe32.szExeFile);
		if (!wcschr(procName, L'.')) {
			WCHAR *s = wcsrchr(szProcessName, L'.');
			if (s)
				*s = L'\0';
		}
		if (_wcsicmp(szProcessName, procName)==0)
			count++;
	}

	CloseHandle(hProcessSnap);
	return count;
}

int wmain(int argc, WCHAR* argv[])
{
	if (argc==1) {
		printf("Usage:\n");
		printf("   BatUtil timerstart timerfile.timer\n");
		printf("   BatUtil timerstop timerfile.timer outfile1.txt outfile2.txt\n");
		printf("   BatUtil WaitForExit wait_ms processname1 [processname2 ...]\n");
		printf("   BatUtil IsRunning processname1 [processname2 ...]\n");
		printf("      Returns 1 if any listed process is running.\n");
		printf("   BatUtil CompareTimestamps file1 file2\n");
		printf("      Returns 1 if file1 is newer than file2, 0 if equal, -1 if older\n");
		return 0;
	}
	if (_wcsicmp(argv[1], L"timerstart")==0)
	{
		DWORD t;
		FILE *f=NULL;
		_wfopen_s(&f, argv[2], L"w");
		t = timeGetTime();
		fprintf(f, "%d", t);
		fclose(f);
	} else if (_wcsicmp(argv[1], L"timerstop")==0)
	{
		DWORD newt, oldt;
		FILE *f=NULL;
		_wfopen_s(&f, argv[2], L"r");
		newt = timeGetTime();
		fscanf_s(f, "%d", &oldt);
		fclose(f);
		_wfopen_s(&f, argv[4], L"w");
		DWORD delta = newt - oldt;
		fprintf(f, "%d", delta);
		fclose(f);
		_wfopen_s(&f, argv[3], L"w");
		int hrs = delta / 3600 / 1000;
		delta -= hrs * 3600 * 1000;
		int mns = delta / 60 / 1000;
		delta -= mns * 60 * 1000;
		int secs = delta / 1000;
		if (hrs) {
			fprintf(f, "%d hours, ", hrs);
		}
		if (hrs || mns) {
			fprintf(f, "%d minutes, ", mns);
		}
		fprintf(f, "%d seconds", secs);
		fclose(f);
	} else if (_wcsicmp(argv[1], L"WaitForExit")==0) 
	{
		DWORD startt;
		DWORD lastPrint=0;
		DWORD wait_ms = _wtoi(argv[2]);
		int count=0;
		startt = timeGetTime();
		printf("Waiting for processes to exit...\n");
		do {
			int i;
			bool printed=false;
			count=0;
			for (i=3; i<argc; i++) {
				int thisone = ProcessCount64(argv[i]);
				count+=thisone;
				if (thisone && ((timeGetTime() - lastPrint) > 1000)) {
					printed = true;
					printf("%s ", argv[i]);
				}
			}
			if (printed)
				lastPrint = timeGetTime();
			if (count!=0)
				startt = timeGetTime();
			Sleep(25);
		} while (count != 0 || timeGetTime() - startt < wait_ms);
	} else if (_wcsicmp(argv[1], L"IsRunning")==0) 
	{
		DWORD startt;
		DWORD lastPrint=0;
		int count=0;
		startt = timeGetTime();
		int i;
		count=0;
		for (i=2; i<argc; i++) {
			int thisone = ProcessCount64(argv[i]);
			count+=thisone;
			if (thisone) {
				printf("%s ", argv[i]);
			}
		}
		return !!count;
	} else if (_wcsicmp(argv[1], L"CompareTimestamps")==0)
	{
		struct _stat64 sbuf1;
		struct _stat64 sbuf2;
		int r1 = _wstat64(argv[2], &sbuf1);
		int r2 = _wstat64(argv[3], &sbuf2);
		if (r1 == -1)
		{
			if (r2 == -1)
				return 0;
			return -1;
		}
		if (r2 == -1)
			return 1;
		if (sbuf1.st_mtime > sbuf2.st_mtime)
			return 1;
		if (sbuf1.st_mtime < sbuf2.st_mtime)
			return -1;
		return 0;
	}
	return 0;
}
