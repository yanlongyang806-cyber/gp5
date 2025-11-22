
#include <stdio.h>
#include <stdlib.h> 
#include <string>
#include <vector>




#include "SimplygonSDKLoader.h"

static std::vector<std::string> &getAdditionalSearchPaths(void) {
	
	static std::vector<std::string> *AdditionalSearchPaths = 0;

	if(AdditionalSearchPaths) return *AdditionalSearchPaths;
	AdditionalSearchPaths = new std::vector<std::string>();
	return *AdditionalSearchPaths;
}

#ifdef _WIN32
#pragma warning (disable:6387)
#include <windows.h>
#include <psapi.h>
#include <shlobj.h>
#include <process.h>
#include <io.h>

extern "C"
{
#include "utf8_prototypes.h"
}



typedef int (CALLBACK* LPINITIALIZESIMPLYGONSDK)( const char *license_data , SimplygonSDK::ISimplygonSDK **pInterfacePtr );
typedef void (CALLBACK* LPDEINITIALIZESIMPLYGONSDK)();
typedef void (CALLBACK* LPGETINTERFACEVERSIONSIMPLYGONSDK)( char *deststring );

#define PUBLICBUILD

static LPINITIALIZESIMPLYGONSDK InitializeSimplygonSDKPtr; 
static LPDEINITIALIZESIMPLYGONSDK DeinitializeSimplygonSDKPtr;
static LPGETINTERFACEVERSIONSIMPLYGONSDK GetInterfaceVersionSimplygonSDKPtr;
static HINSTANCE hDLL = NULL; // Handle to SimplygonSDK DLL

// critical sections, process-local mutexes
class rcriticalsection
	{
	private:
		CRITICAL_SECTION cs;

	public:
		rcriticalsection() { ::InitializeCriticalSection(&cs); }
		~rcriticalsection() { ::DeleteCriticalSection(&cs); }

		void Enter() { EnterCriticalSection(&cs); }
		void Leave() { LeaveCriticalSection(&cs); }
	};

static int GetStringFromRegistry( const char *keyid , const char *valueid , char *dest )
	{
	HKEY reg_key;
	if( RegOpenKey_UTF8( HKEY_LOCAL_MACHINE , keyid , &reg_key ) != ERROR_SUCCESS )
		return SimplygonSDK::SG_ERROR_NOLICENSE; 

	// read the value from the key
	DWORD path_size = MAX_PATH;
	if( RegQueryValueEx_UTF8( reg_key , valueid , NULL ,	NULL , (unsigned char *)dest , &path_size ) != ERROR_SUCCESS )
		return SimplygonSDK::SG_ERROR_NOLICENSE;
	dest[path_size] = 0;

	// close the key
	RegCloseKey( reg_key );
	return SimplygonSDK::SG_ERROR_NOERROR;
	}

static bool FileExists( const char *path )
	{
	DWORD v = ::GetFileAttributes_UTF8( path );
	if( v == INVALID_FILE_ATTRIBUTES )
		{
		return false;
		}
	return true;
	}


static std::string GetInstallationPath()
	{
	char InstallationPath[MAX_PATH];

	// get the installation path string from the registry
	if( GetStringFromRegistry( "Software\\DonyaLabs\\SimplygonSDK" , "InstallationPath" , InstallationPath ) != 0 )
		{
		return "";
		}

	size_t t = strlen(InstallationPath);
	if( InstallationPath[t] != '\\' )
		{
		InstallationPath[t] = '\\';
		InstallationPath[t+1] = '\0';
		}

	// append a backslash
	return InstallationPath;
	}

static std::string GetLicensePath()
	{
	WCHAR AppDataPath_Wide[MAX_PATH];
	char AppDataPath_Narrow[MAX_PATH];

	// Get the common appdata path
	if( SHGetFolderPath( NULL, CSIDL_COMMON_APPDATA, NULL, 0, AppDataPath_Wide ) != 0 )
	{
	return "";
	}

	WideToUTF8StrConvert(AppDataPath_Wide, AppDataPath_Narrow, MAX_PATH);

	// append the path to Simplygon SDK
	return std::string(AppDataPath_Narrow) + "\\DonyaLabs\\SimplygonSDK\\";
	}

static std::string GetApplicationPath()
	{
	char *pPath = NULL;
	char Path[MAX_PATH];
	char drive[_MAX_DRIVE];
	char dir[_MAX_DIR];
	char fname[_MAX_FNAME];
	char ext[_MAX_EXT];

	if( GetModuleFileName_UTF8( NULL , &pPath ) == NULL )
	{	
		estrDestroy(&pPath);
		return "";
	}

	strcpy_s(Path, MAX_PATH, pPath);
	estrDestroy(&pPath);

	_splitpath_s( Path, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT );
	_makepath_s( Path, _MAX_PATH, drive, dir, "", "" );

	return Path;
	}

static std::string GetWorkingDirectory()
	{
	char dir[MAX_PATH];
	char *pDir = NULL;
	GetCurrentDirectory_UTF8(&pDir);
	strcpy_s(dir, MAX_PATH, pDir ? pDir : "");
	estrDestroy(&pDir);
	return dir;
	}

static std::string GetLibraryDirectory( std::string path )
	{
	char Directory[_MAX_PATH];
	char drive[_MAX_DRIVE];
	char dir[_MAX_DIR];
	char fname[_MAX_FNAME];
	char ext[_MAX_EXT];

	_splitpath_s( path.c_str(), drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT );
	_makepath_s( Directory, _MAX_PATH, drive, dir, "", "" );

	return Directory;
	}

static void LogDLLModuleInfo( HMODULE hTargetDLL )
{
	HMODULE hPsapi = LoadLibrary( L"psapi.dll" );
	if ( hPsapi != 0 )
	{
		typedef BOOL (__stdcall *tGMI)( HANDLE hProcess, HMODULE hModule, LPMODULEINFO pmi, DWORD nSize );

		tGMI pGMI = (tGMI) GetProcAddress( hPsapi, "GetModuleInformation" );
		MODULEINFO module_info = { 0 };
		if ((*pGMI)( GetCurrentProcess(), hTargetDLL, &module_info, sizeof(module_info)))
		{
			char buffer[ 400 ];
			sprintf_s(buffer, 400, "Simplygon DLL Base @0x%p, End 0x%p\n", module_info.lpBaseOfDll,
				(char*)module_info.lpBaseOfDll + module_info.SizeOfImage);
			OutputDebugString_UTF8(buffer);
		}
		else
		{
			OutputDebugString(L"GetModuleInformation failed\n");
		}
		FreeLibrary( hPsapi );
	}
	else
	{
		OutputDebugString(L"LoadLibrary(psapi.dll) failed\n");
	}
}

static bool LoadOSLibrary( std::string path )
	{
	std::string lib_dir = GetLibraryDirectory( path );
	std::string curr_dir = GetWorkingDirectory();

	// move to the dir where the library is 
	SetCurrentDirectory_UTF8( lib_dir.c_str() );

	// try loading the library
	if( FileExists( path.c_str() ) )
		{
		hDLL = LoadLibrary_UTF8( path.c_str() );
		if( !hDLL )
			{
			// something went wrong, check what
			DWORD err = GetLastError();
			char buffer[400];
			if( err == 126 )
				{
					sprintf_s(buffer, 400, "Loading the Simplygon DLL failed, which is in most cases because of a missing dependency, are all dependencies installed? Please try reinstalling Simplygon.");
				}
			else
				{
					sprintf_s(buffer, 400, "Load failed, last error = %d",err);
				}
			printf("%s\n", buffer);
			}
		else
			{
			LogDLLModuleInfo(hDLL);
			}
		}
/*	else
		{
		DWORD err = GetLastError();
		char buffer[400];
		sprintf(buffer,"No file, last error = %d",err);
		::MessageBox(NULL,buffer,"",MB_OK);
		}
*/
	
	// move back to the previous dir
	SetCurrentDirectory_UTF8( curr_dir.c_str() );

	// return success or failure
	if( hDLL )
		return true;
	return false;
	}

static bool FindNamedDynamicLibrary( std::string DLLName )
	{
	std::string InstallationPath = GetInstallationPath();
	std::string ApplicationPath = GetApplicationPath();
	std::string WorkingPath = GetWorkingDirectory() + "/";

	// try additional paths first, so they are able to override
	for( size_t i=0; i<getAdditionalSearchPaths().size(); ++i )
		{
		if( LoadOSLibrary( getAdditionalSearchPaths()[i] + DLLName ) )
			{
			return true;
			}
		}

	// try local application path first
	if( LoadOSLibrary( ApplicationPath + DLLName ) )
		{
		return true;
		}
	
	// next try installation path
	if( !InstallationPath.empty() )
		{
		if( LoadOSLibrary( InstallationPath + DLLName ) )
			{
			return true;
			}
		}

	// try working directory as well
	if( !WorkingPath.empty() )
		{
		if( LoadOSLibrary( WorkingPath + DLLName ) )
			{
			return true;
			}
		}

	return false;
	}

static bool FindDynamicLibrary( const char *SDKPath )
	{
	std::string DLLName;
	
	if( SDKPath != NULL )
		{
		// load from explicit place, fail if not found
		if( LoadOSLibrary( std::string(SDKPath) ) )
			{
			return true;
			}
		return false;
		}
		
	// if debug version, try debug build first
#ifdef _DEBUG

#ifdef _WIN64
	DLLName = "SimplygonSDKRuntimeDebugx64.dll";
#else
	DLLName = "SimplygonSDKRuntimeDebugWin32.dll";
#endif

	if( FindNamedDynamicLibrary( DLLName ) )
		{
		return true;
		}

#endif//_DEBUG

	// next (or if release) try release builds
#ifdef _WIN64
	DLLName = "SimplygonSDKRuntimeReleasex64.dll";
#else
	DLLName = "SimplygonSDKRuntimeReleaseWin32.dll";
#endif

	if( FindNamedDynamicLibrary( DLLName ) )
		{
		return true;
		}

	return false;
	}

static bool LoadDynamicLibrary( const char *SDKPath )
	{
	if( !FindDynamicLibrary(SDKPath) )
		{
		return false;
		}

	// setup the function pointers
	InitializeSimplygonSDKPtr = (LPINITIALIZESIMPLYGONSDK)GetProcAddress(hDLL,"InitializeSimplygonSDK");
	DeinitializeSimplygonSDKPtr = (LPDEINITIALIZESIMPLYGONSDK)GetProcAddress(hDLL,"DeinitializeSimplygonSDK");
	GetInterfaceVersionSimplygonSDKPtr = (LPGETINTERFACEVERSIONSIMPLYGONSDK)GetProcAddress(hDLL,"GetInterfaceVersionSimplygonSDK");
	if( InitializeSimplygonSDKPtr==NULL || 
		DeinitializeSimplygonSDKPtr==NULL ||
		GetInterfaceVersionSimplygonSDKPtr==NULL )
		{
		return false;
		}
	return true;
	}

static bool DynamicLibraryIsLoaded()
	{
	if(hDLL == NULL)
		{
		return false;
		}	
	return true;
	}

static void UnloadDynamicLibrary()
	{
	FreeLibrary(hDLL);
	hDLL = NULL;	

	// Log if Simplygon dummy window class is still registered
	WNDCLASS windowClassSG = { 0 };
	if (GetClassInfo(GetModuleHandle(NULL), L"SimplygonDummy", &windowClassSG))
		{
		OutputDebugString(L"Simplygon left window class registered, automatically unregistering it.\n");
		UnregisterClass(L"SimplygonDummy", GetModuleHandle(NULL));
		}
	}

#ifdef PUBLICBUILD

static bool ReadLicense( std::string LicensePath , char **dest )
{
	FILE *fp = NULL;

	if( fopen_s(&fp,LicensePath.c_str(),"rb") != 0 )
	{
		return false;
	}

	int len = _filelength(fp->_file);
	*dest = (char*)malloc(len+1);

	if(*dest) {

		if((int)fread(*dest,1,len,fp) != len ) {
			fclose(fp);
			return false;
		}

		(*dest)[len] = '\0';
		fclose(fp);
		return true;
	}

	return false;
}

static bool LoadLicense( char **dest )
	{
	// try the local folder first
	if( ReadLicense( GetApplicationPath() + "License.dat" , dest ) )
		{
		return true;
		}

	// next, try the common application folder
	if( ReadLicense( GetLicensePath() + "License.dat" , dest ) )
		{
		return true;
		}

	return false;
	}

#endif//PUBLICBUILD

#endif//_WIN32

#ifdef __linux__

#include<dlfcn.h>
#include <pthread.h>

typedef int (*LPINITIALIZESIMPLYGONSDK)( const char *license_data , SimplygonSDK::ISimplygonSDK **pInterfacePtr );
typedef void (*LPDEINITIALIZESIMPLYGONSDK)();
typedef void (*LPGETINTERFACEVERSIONSIMPLYGONSDK)( char *deststring );

static LPINITIALIZESIMPLYGONSDK InitializeSimplygonSDKPtr; 
static LPDEINITIALIZESIMPLYGONSDK DeinitializeSimplygonSDKPtr;
static LPGETINTERFACEVERSIONSIMPLYGONSDK GetInterfaceVersionSimplygonSDKPtr;
static void *hSO = NULL; // Handle to SimplygonSDK SO

// critical sections, process-local mutexes
class rcriticalsection
	{
	private:
		pthread_mutex_t mutex;

	public:
		rcriticalsection() 
			{ 
			::pthread_mutexattr_t mutexattr;
			::pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE_NP);
			::pthread_mutex_init(&mutex, &mutexattr);
			::pthread_mutexattr_destroy(&mutexattr);
			}
		~rcriticalsection() 
			{ 
			::pthread_mutex_destroy(&mutex); 
			}

		void Enter() { pthread_mutex_lock(&mutex); }
		void Leave() { pthread_mutex_unlock(&mutex); }
	};

static bool dlErrorHandler()
	{
	char *dlError = dlerror();
	if(dlError) 
		{
		fprintf(stderr,"dlErrorHandler(): Error with dl function: %s\n", dlError);
		return false;
		}
	return true; // all ok
	}

static bool LoadDynamicLibrary( const char *SDKPath )
	{
	if( SDKPath != NULL )
		{
		hSO = dlopen(SDKPath, RTLD_NOW);
		}
	else
		{
		hSO = dlopen("./libSimplygonSDK.so", RTLD_NOW);
		}
	
	if( !dlErrorHandler() )
		{
		return false;
		}

	// setup the function pointers
	InitializeSimplygonSDKPtr = (LPINITIALIZESIMPLYGONSDK)dlsym(hSO,"InitializeSimplygonSDK");
	DeinitializeSimplygonSDKPtr = (LPDEINITIALIZESIMPLYGONSDK)dlsym(hSO,"DeinitializeSimplygonSDK");
	GetInterfaceVersionSimplygonSDKPtr = (LPGETINTERFACEVERSIONSIMPLYGONSDK)dlsym(hSO,"GetInterfaceVersionSimplygonSDK");
	if( InitializeSimplygonSDKPtr==NULL || 
		DeinitializeSimplygonSDKPtr==NULL ||
		GetInterfaceVersionSimplygonSDKPtr==NULL )
		{
		dlErrorHandler();
		fprintf(stderr,"LoadDynamicLibrary(): Failed to retrieve all pointers.\n");
		return false;
		}
	return true;
	}

static bool DynamicLibraryIsLoaded()
	{
	if(hSO == NULL)
		{
		return false;
		}	
	return true;
	}

static void UnloadDynamicLibrary()
	{
	dlclose(hSO);
	hSO = NULL;	
	}

#endif //__linux__


int InitializeSimplygonSDK( SimplygonSDK::ISimplygonSDK **pInterfacePtr , const char *SDKPath, char *LicenseData)
	{

	bool freeLicenseData = !LicenseData;

	// load the library
	if( !LoadDynamicLibrary(SDKPath) )
		{
		return SimplygonSDK::SG_ERROR_FILENOTFOUND;
		}	

	// read the version string, compare to interface version string
	char versionstring[200];
	GetInterfaceVersionSimplygonSDKPtr( versionstring );
	const SimplygonSDK::rchar *header_version = SimplygonSDK::GetInterfaceVersionHash();

	// This is just to make Visual Studio stop complaining.
	versionstring[199] = 0;

	if( strcmp(versionstring,header_version) != 0 )
		{
		return SimplygonSDK::SG_ERROR_WRONGVERSION;
		}

	// read the license file from the installation path
#if defined(_WIN32) && defined(PUBLICBUILD)
	if(!LicenseData) {
		if(!LoadLicense(&LicenseData)) {
			return SimplygonSDK::SG_ERROR_NOLICENSE;
		}
	}
#endif

	// call the initalization function
	int retval = InitializeSimplygonSDKPtr(LicenseData,pInterfacePtr);

	// clean up the allocated data
	if( LicenseData && freeLicenseData )
		{
		free(LicenseData);
		}

	// return retval
	return retval;
	}

void DeinitializeSimplygonSDK()
	{
	if( !DynamicLibraryIsLoaded() )
		{
		return;
		}
	DeinitializeSimplygonSDKPtr();
	UnloadDynamicLibrary();
	}

////////////////////////////////////////////////////////////////////////////////////

namespace SimplygonSDK
	{
	static rcriticalsection init_guard; // only one thread is allowed to run init/deinit at any time
	static int init_count = 0; // a reference count of the number of Init/Deinits
	static ISimplygonSDK *InterfacePtr; // the pointer to the only interface

	void AddSearchPath( const char *search_path )
		{
		getAdditionalSearchPaths().push_back( std::string( search_path) );
		}

		int Initialize( ISimplygonSDK **pInterfacePtr , const char *SDKPath, char *LicenseData )
		{
		int retval = SG_ERROR_NOERROR;

		// if the interface does not exist, init it
		if( InterfacePtr == NULL )
			{
			init_count = 0;
			retval = ::InitializeSimplygonSDK( &InterfacePtr , SDKPath , LicenseData );
			}

		// if everything is ok, add a reference
		if( retval == SG_ERROR_NOERROR )
			{
			++init_count;
			*pInterfacePtr = InterfacePtr;
			}

		return retval;
		}

	void Deinitialize()
		{
		// release a reference
		--init_count;

		// if the reference is less or equal to 0, release the interface 
		if( init_count <= 0 )
			{
			// only release if one exists
			if( InterfacePtr != NULL )
				{
				::DeinitializeSimplygonSDK();
				InterfacePtr = NULL;
				}

			// make sure the init_count is 0
			init_count = 0;
			}
		}

	const char *GetError( int error_code )
		{
		switch( error_code ) 
			{
			case SG_ERROR_NOERROR:
				return "No error";
			case SG_ERROR_NOLICENSE:
				return "No license was found, or the license is not valid. Please install a valid license.";
			case SG_ERROR_NOTINITIALIZED:
				return "The SDK is not initialized, or no process object is loaded.";
			case SG_ERROR_ALREADYINITIALIZED:
				return "The SDK is already initialized. Please call Deinitialize() before calling Initialize() again.";
			case SG_ERROR_FILENOTFOUND:
				return "The specified file was not found.";
			case SG_ERROR_INVALIDPARAM:
				return "An invalid parameter was passed to the method";
			case SG_ERROR_WRONGVERSION:
				return "The Simplygon DLL and header file interface versions do not match";

			default:
				return "Invalid error code";
			}
		}

	};

