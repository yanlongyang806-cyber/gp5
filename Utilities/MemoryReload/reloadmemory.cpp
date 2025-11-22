// reloadmemory.cpp : Defines the entry point for the console application.
//

#include <windows.h>
#include <Mmsystem.h>
#include <Tlhelp32.h>
#include <conio.h>
#include <stdio.h>

#pragma comment(lib, "winmm.lib")

typedef unsigned __int64	U64;
typedef int					S32;
typedef unsigned int		U32;

#define ARRAY_SIZE(n)							(sizeof(n) / sizeof((n)[0]))

char* getCommaSeparatedU64(U64 x){
	static int curBuffer = 0;
	// 27+'\0' is the max length of a 64bit value with commas.
	static char bufferArray[10][30]; 
	
	char*	buffer = bufferArray[curBuffer = (curBuffer + 1) % ARRAY_SIZE(bufferArray)];
	S32		digits = 0;
	
	buffer += ARRAY_SIZE(bufferArray[0]) - 1;

	*buffer-- = 0;
	
	do{
		*buffer-- = '0' + (char)(x % 10);

		x = x / 10;

		if(x && ++digits == 3){
			digits = 0;
			*buffer-- = ',';
		}
	}while(x);
	
	return buffer + 1;
}

int main(int argc, char* argv[])
{
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 pe;
	int good;
	char* buffer;
	SYSTEM_INFO si;
	
	if(argc < 2){
		printf("Usage: reloadmemory <name.exe>\n");
		exit(1);
	}
	
	GetSystemInfo(&si);
	
	buffer = (char*)malloc(si.dwPageSize);
	
	SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
	
	if(hSnap == INVALID_HANDLE_VALUE){
		printf("Can't get process image.\n");
	}else{
		for(good = Process32First(hSnap, &pe); good; good = Process32Next(hSnap, &pe)){
			int		pid = pe.th32ProcessID;
			HANDLE	hProcess = OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_INFORMATION , FALSE, pid);
			
			printf("%5d - %s:", pid, pe.szExeFile);
			
			if(	!hProcess ||
				_stricmp(pe.szExeFile, argv[1]))
			{
				printf("SKIPPED!\n");
			}else{
				char* base = 0;
				U64 bytes = 0;
				U64 bytesChecked = 0;
				U32 lastTime = timeGetTime() - 1000;
				
				for(base = (char*)(uintptr_t)si.dwPageSize; base; base += si.dwPageSize){
					SIZE_T outsize;
					
					if(ReadProcessMemory(hProcess, base, buffer, si.dwPageSize, &outsize)){
						bytesChecked += si.dwPageSize;
						bytes += si.dwPageSize;
					}else{
						MEMORY_BASIC_INFORMATION mbi;

						if(VirtualQueryEx(hProcess, base, &mbi, sizeof(mbi))){
							#if 0
							{
								printf(	"base 0x%p: Skipping 0x%8.8x bytes at 0x%p to 0x%p\n",
										base,
										mbi.RegionSize,
										mbi.BaseAddress,
										(char*)mbi.BaseAddress + mbi.RegionSize);
							}
							#endif

							base = (char*)mbi.BaseAddress + mbi.RegionSize;
							
							bytesChecked += mbi.RegionSize;
						}else{
							U32 error = GetLastError();
							
							bytesChecked += si.dwPageSize;
						}
					}

					if(timeGetTime() - lastTime >= 300){
						char title[1000];
						lastTime = timeGetTime();
						sprintf_s(	title,
									sizeof(title),
									"Reload Memory, %s, pid %d: %s bytes (%s checked)",
									pe.szExeFile,
									pe.th32ProcessID,
									getCommaSeparatedU64(bytes),
									getCommaSeparatedU64(bytesChecked));
						SetConsoleTitle(title);
						printf(".");
						Sleep(25);
					}
				}
				
				printf(" read %s bytes\n", getCommaSeparatedU64(bytes));
				
				CloseHandle(hProcess);
			}
		}
	
		CloseHandle(hSnap);
	}
	
	SetConsoleTitle("Rld: Done!!!");
	
	printf("Done!!!\n");
	
	_getch();
	
	return 0;
}

