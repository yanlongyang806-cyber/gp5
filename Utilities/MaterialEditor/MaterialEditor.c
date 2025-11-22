//#include <string.h>
#include "wininclude.h"
#include "winutil.h"
#include "file.h"
#include "utils.h"
#include "utf8.h"

int __stdcall wWinMain(__in HINSTANCE hInstance, __in_opt HINSTANCE hPrevInstance, WCHAR*    pWideCmdLine, __in int nShowCmd)
{
	char moduleFilename[MAX_PATH*2];
	char *s;
	PROCESS_INFORMATION pi={0};
	STARTUPINFOA si={0};
	bool bFoundIt=false;
	char *lpCmdLine = UTF16_to_UTF8_CommandLine(pWideCmdLine);

	DO_AUTO_RUNS;

	fileAutoDataDir();

	winRegisterMe("MaterialEditor", ".Material");
	winRegisterMe("Open", ".Material");

	if (stricmp(lpCmdLine, "-register")==0)
		return 0;

	if (!bFoundIt && fileToolsBinDir())
	{
		sprintf(moduleFilename, "%s/AssetManager.exe", fileToolsBinDir());
		if (fileExists(moduleFilename))
			bFoundIt = true;
	}

	if (!bFoundIt && fileCoreToolsBinDir())
	{
		sprintf(moduleFilename, "%s/AssetManager.exe", fileCoreToolsBinDir());
		if (fileExists(moduleFilename))
			bFoundIt = true;
	}

	if (!bFoundIt)
	{
		GetModuleFileNameA(NULL, moduleFilename, MAX_PATH);
		s = strrchr(moduleFilename, '\\')+1;
		*s='\0';
		strcat(moduleFilename, "AssetManager.exe");
	}

	strcat(moduleFilename, " -notools -StartMatEd -maxInactiveFPS 2");

	if (stricmp(lpCmdLine, "")!=0) {
		strcat(moduleFilename, " -MatEd2Open ");
		if (lpCmdLine[0]!='\"')
			strcat(moduleFilename, "\"");
		strcat(moduleFilename, lpCmdLine);
		if (lpCmdLine[0]!='\"')
			strcat(moduleFilename, "\"");
	}
	backSlashes(moduleFilename);
	CreateProcessA(NULL, moduleFilename, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return 0;
}
