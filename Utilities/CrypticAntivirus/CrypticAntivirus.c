// CrypticAntivirus.cpp : Framework for ad hoc virus scanning
// Currently, this is designed to look only for a specific Zeus variant, ZEUS.CRYPTIC.

#include <windows.h>
#include <Mmsystem.h>
#include <Tlhelp32.h>
#include <conio.h>
#include <stdio.h>
#include <Shlwapi.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "wsock32.lib")

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

// From utils_opt.c
// Like strstr(), but the string to be searched is not null-terminated.
// This function could be further optimized, if necessary.
static char *memstr(const char *haystack, const char *needle, size_t haystack_size)
{
	size_t needle_len = strlen(needle);
	size_t i;

	if (!needle_len)
		return (char *)haystack;

	if (haystack_size < needle_len)
		return NULL;

	for (i = 0; i != haystack_size - needle_len + 1; ++i)
	{
		if (!memcmp(haystack + i, needle, needle_len))
			return (char *)(haystack + i);
	}

	return NULL;
}

static BOOL isDefender(DWORD pid)
{
	MODULEENTRY32 mod;
	BOOL result;
	HANDLE hSnapshot;
	mod.dwSize = sizeof(mod);

	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);

	result = Module32First(hSnapshot, &mod);

	while(result)
	{
		if (StrStrI(mod.szModule, "mpclient"))
		{
			CloseHandle(hSnapshot);
			return TRUE;
		}
		result = Module32Next(hSnapshot, &mod);
	}

	CloseHandle(hSnapshot);

	return FALSE;
}

static BOOL antivirus(int *found)
{
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 pe;
	int good;
	char* buffer;
	char* buffer2;
	SYSTEM_INFO si;
	BOOL success = 0;
	int procs = 0;
	U64 totalbytesChecked = 0;

	// 00000000`023a9766 33c0            xor     eax,eax
	// 00000000`023a9768 488bc8          mov     rcx,rax
	// 00000000`023a976b ff1537cf0100    call    qword ptr [00000000`023c66a8]
	// 00000000`023a9771 4885c0          test    rax,rax
	// 00000000`023a9774 7426            je      00000000`023a979c
	static char pattern[] = {0x33, 0xc0, 0x48, 0x8b, 0xc8, 0xff, 0x15, 0x37, 0xcf, 0x01, 0x00, 0x48, 0x85, 0xc0, 0x74, 0x26, 0};

	//static char pattern2[] = {0x7c, 0xe2, 0x83, 0x3d, 0x67, 0x14, 0x03, 0};
	//static char pattern2[] = "http://code.google.com/p/get-green/";
	//static char pattern2[] = "RE9DSFMtUVlSWEEtTFZQQU8tVEVDS0QtSk1YSEI=";
	static char pattern2[] = "183.111.128.42:80";
	
	GetSystemInfo(&si);
	
	buffer = (char*)malloc(si.dwPageSize);
	buffer2 = (char*)calloc(si.dwPageSize*2,1);
	
	SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
	
	if(hSnap == INVALID_HANDLE_VALUE){
		printf("Can't get snapshot image.\n");
	}else{
		for(pe.dwSize = sizeof(pe), good = Process32First(hSnap, &pe); good; pe.dwSize = sizeof(pe), good = Process32Next(hSnap, &pe)){
			int		pid = pe.th32ProcessID;
			HANDLE	hProcess = OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_INFORMATION , FALSE, pid);
			
			//printf("%5d - %s:\n", pid, pe.szExeFile);
			
			if(	hProcess)
			{
				int relevant_process = (!_stricmp(pe.szExeFile, "explorer.exe") ||
					!_stricmp(pe.szExeFile, "svchost.exe")) && !isDefender(pe.th32ProcessID);
				if (relevant_process)
				{
					char* base = 0;
					U64 bytes = 0;
					U64 bytesChecked = 0;
					U32 lastTime = timeGetTime() - 1000;
					MEMORY_BASIC_INFORMATION mbi;
					char *last = (char *)42;

					//base = (char*)(uintptr_t)si.dwPageSize;
					//base = (char*)0x10016;
					base = (char*)0x1;

					++procs;

					for(;;){
						SIZE_T ret;
						SIZE_T outsize;

						SetLastError(0);
						ret = VirtualQueryEx(hProcess, base, &mbi, sizeof(mbi));

						if(mbi.BaseAddress == last)
						{
// 							printf("  %s done, pid %d: %d procs, %s bytes\n",
// 								pe.szExeFile,
// 								pe.th32ProcessID,
// 								procs,
// 								getCommaSeparatedU64(bytesChecked));
							break;
						}

						last = mbi.BaseAddress;
						
						//printf("\nP: %p\n", mbi.BaseAddress);

						for (base = mbi.BaseAddress; mbi.State == MEM_COMMIT && base < (char *)mbi.BaseAddress + mbi.RegionSize; base += si.dwPageSize)
						{

							if(ReadProcessMemory(hProcess, base, buffer, si.dwPageSize, &outsize)){

								// Record statistics.
								bytesChecked += si.dwPageSize;
								bytes += si.dwPageSize;
								totalbytesChecked += si.dwPageSize;

								// Set up comparison buffer.
								memcpy(buffer2, buffer2 + si.dwPageSize, si.dwPageSize);
								memcpy(buffer2 + si.dwPageSize, buffer, si.dwPageSize);

								// Do comparison.
								if (memstr(buffer2, pattern, si.dwPageSize*2))
								{
									*found |= 1;
								}
								if (memstr(buffer2, pattern2, si.dwPageSize*2))
								{
									*found |= 2;
								}

							}else{
#if 0
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
#endif
							}

							if(timeGetTime() - lastTime >= 300){
								char title[1000];
								lastTime = timeGetTime();
								sprintf_s(	title,
									sizeof(title),
									//									"CrypticAntivirus, %s, pid %d: %s bytes (%s checked)",
									"CrypticAntivirus, %s, pid %d: %d procs, %s bytes, pointer %p",
									pe.szExeFile,
									pe.th32ProcessID,
									procs,
									getCommaSeparatedU64(bytesChecked),
									base);
								SetConsoleTitle(title);
								//printf(".");
								//Sleep(25);
							}
						}

						//base += si.dwPageSize;
						base = (char *)mbi.BaseAddress + mbi.RegionSize;
					}

					//printf(" read %s bytes\n", getCommaSeparatedU64(bytes));

					if (bytesChecked > 0)
						success = 1;

					//break;
				}
				CloseHandle(hProcess);
			}
		}
	
		CloseHandle(hSnap);
	}
	
	SetConsoleTitle("Rld: Done!!!");

// 	printf("CrypticAntivirus, %s, pid %d: %d procs, %s bytes\n",
// 		pe.szExeFile,
// 		pe.th32ProcessID,
// 		procs,
// 		getCommaSeparatedU64(totalbytesChecked));
	
	//printf("Done!!!\n");

	return success;
}

static int isSupported(int *maybe)
{
	//BOOL f64;
	//BOOL supported = IsWow64Process(GetCurrentProcess(), &f64) && f64;
	DWORD ver;

	//if (!supported)
	//	return 0;

	ver = GetVersion();
	*maybe = !((ver&0xffff) == 0x0106);

	return 1;
}

// From superassert.c
static void	wsockStart()
{
	WORD wVersionRequested;  
	WSADATA wsaData; 
	int err; 
	wVersionRequested = MAKEWORD(2, 2); 

	err = WSAStartup(wVersionRequested, &wsaData); 
}

static void print_name()
{
	char name[256];
	wsockStart();
	gethostname(name, sizeof(name));
	printf("v8 %s: ", name);
}

BOOL SetPrivilege(
	HANDLE hToken,          // token handle
	LPCTSTR Privilege,      // Privilege to enable/disable
	BOOL bEnablePrivilege   // TRUE to enable.  FALSE to disable
	)
{
	TOKEN_PRIVILEGES tp;
	LUID luid;
	TOKEN_PRIVILEGES tpPrevious;
	DWORD cbPrevious=sizeof(TOKEN_PRIVILEGES);

	if(!LookupPrivilegeValue( NULL, Privilege, &luid )) return FALSE;

	// 
	// first pass.  get current privilege setting
	// 
	tp.PrivilegeCount           = 1;
	tp.Privileges[0].Luid       = luid;
	tp.Privileges[0].Attributes = 0;

	AdjustTokenPrivileges(
		hToken,
		FALSE,
		&tp,
		sizeof(TOKEN_PRIVILEGES),
		&tpPrevious,
		&cbPrevious
		);

	if (GetLastError() != ERROR_SUCCESS) return FALSE;

	// 
	// second pass.  set privilege based on previous setting
	// 
	tpPrevious.PrivilegeCount       = 1;
	tpPrevious.Privileges[0].Luid   = luid;

	if(bEnablePrivilege) {
		tpPrevious.Privileges[0].Attributes |= (SE_PRIVILEGE_ENABLED);
	}
	else {
		tpPrevious.Privileges[0].Attributes ^= (SE_PRIVILEGE_ENABLED &
			tpPrevious.Privileges[0].Attributes);
	}

	AdjustTokenPrivileges(
		hToken,
		FALSE,
		&tpPrevious,
		cbPrevious,
		NULL,
		NULL
		);

	if (GetLastError() != ERROR_SUCCESS) return FALSE;

	return TRUE;
}

static void get_privs()
{
	HANDLE hToken;

	if(!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &hToken))
	{
		if (GetLastError() == ERROR_NO_TOKEN)
		{
			if (!ImpersonateSelf(SecurityImpersonation))
				return;

			if(!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &hToken)){
				//DisplayError("OpenThreadToken");
				return;
			}
		}
		else
			return;
	}

	// enable SeDebugPrivilege
	if(!SetPrivilege(hToken, SE_DEBUG_NAME, TRUE))
	{
		//DisplayError("SetPrivilege");

		// close token handle
		CloseHandle(hToken);

		// indicate failure
		return;
	}

	printf("(got privs) ");
}

int main(int argc, char* argv[])
{
	int supported;
	int maybe_supported;
	int found = 0;
	BOOL success;

	// Print machine name.
	print_name();

	// Check if we're supported.
	supported = isSupported(&maybe_supported);
	if (!supported)
	{
		printf("UNSUPPORTED\n");
		return;
	}
	if (maybe_supported)
		printf("MAYBE SUPPORTED: ");
	else
		printf("SUPPORTED: ");

	// Get elevated privileges.
	get_privs();

	// Run the scan.
	success = antivirus(&found);
	if (!success)
		printf("Scan failed.\n");
	else
	{
		if (found & 1)
			printf("Infected Zeus.Cryptic ");
		if (found & 2)
			printf("Infected with GetGreen.Cryptic ");
		if (found >= 4)
			printf("Infected with UNKNOWN.Cryptic ");
		if (!found)
			printf("No virus detected.");
		printf("\n");
	}

	return 0;
}
