#include "wininclude.h"
#include "file.h"
#include "sysutil.h"
#include "AppRegCache.h"
#include "earray.h"
#include "utils.h"
#include "fileutil2.h"
#include "cmdparse.h"

#pragma comment(lib,"../../libs/AttachToDebuggerLib/debug/AttachToDebuggerLib.lib")
#pragma comment(lib,"psapi.lib")
#pragma warning(disable:6262)

bool AttachDebugger(long iProcessID);

#define myassert(exp) (exp || (printf("%s", "Assert failed: " #exp "\n"), exit(2), 0))

void AttachDebuggerToSelf(void)
{
	AttachDebugger(GetCurrentProcessId());
	while( !IsDebuggerPresent())
	{
		Sleep(1);
	}
	Sleep(1);
}

bool doesShareExist(const char *sharename)
{
	char buf[1024];
	int ret;
	sprintf(buf, "net share %s >nul 2>nul", sharename);
	ret = system(buf);
	if (ret==0)
		return true;
	return false;
}

void getShareName(const char *folder, char *share_out, size_t share_out_size)
{
	char *s = strchr(folder, '/');
	if (!s) {
		strcpy_s(share_out, share_out_size, folder);
	} else {
		strcpy_s(share_out, share_out_size, s+1);
	}
	myassert(!strchr(share_out, '/'));
	// Make sure it's shared!
	if (!doesShareExist(share_out)) {
		char buf[1024];
		printf("Sharing %s as %s\n", folder, share_out);
		sprintf(buf, "net SHARE %s=%s", share_out, folder);
		backSlashes(buf);
		system(buf);
	} else {
		printf("%s already shared.\n", folder);
	}
}

char projectName[MAX_PATH]="";
char extraParams[1024]="";

AUTO_CMD_STRING(projectName, folder);
AUTO_CMD_STRING(extraParams, extraParams);


int main(int argc, char *argv[])
{
	const char * const *gameDataDirs;
	char newGameDataDirs[10*1024]="";
	char hostname[2048];
	int i;
	FILE *file;
	char tempfile[2048];
	char cmd[2048];
	int ret;

	DO_AUTO_RUNS;

	cmdParseCommandLine(argc, argv);

	//AttachDebuggerToSelf();
	regSetAppName("GameclientXboxPostBuild");

	// Get name of project we're on (unless overridden on the command line)
	if (!projectName[0])
	{
		char **names = fileScanDirNoSubdirRecurse(".");
		char *projname=NULL;
		char *s;
		for (i=0; i<eaSize(&names); i++) {
			if (strEndsWith(names[i], ".vcproj")) {
				if (projname) {
					printf("Error!  Found two .vcproj files in the working directory, don't know what Xbox360 folder to put the GameDataDir in.\n");
					fileScanDirFreeNames(names);
					return 1;
				}
				projname = names[i];
			}
		}
		if (!projname) {
			printf("Error!  Could not find any .vcproj files in the working directory, don't know what Xbox360 folder to put the GameDataDir in.\n");
			return 1;
		}
		strcpy(projectName, projname);
		forwardSlashes(projectName);
		s = strrchr(projectName, '/');
		if (s)
			strcpy(projectName, s+1);
		s = strrchr(projectName, '.');
		if (s)
			*s = '\0';
		fileScanDirFreeNames(names);
	}

	// Get the PC gamedatadir

	if (!fileExists("./gamedatadir.txt")) {
		int temp = chdir("../bin");
	}
	fileAutoDataDir();
	filePrintDataDirs();
	gameDataDirs = fileGetGameDataDirs();
	if (eaSize(&gameDataDirs) == 1 && gameDataDirs[0][0]=='.') {
		 // Error!
	}
	sprintf(hostname, "net:\\smb\\%s\\", getComputerName());
	for (i=0; i<eaSize(&gameDataDirs); i++) {
		char folder[2048];
		char share[2048];
		char newpath[2048];
		char *s;
		if (gameDataDirs[i][0] == '.') {
			printf("Warning: could not determine appropriate game data dir\n");
			printf("NOT copying gamedatadir.txt.\n");
			return 0;
		}
		strcpy(folder, gameDataDirs[i]);
		forwardSlashes(folder);
		s = strchr(folder, '/');
		if (s)
			s = strchr(s+1, '/');
		if (!s) {
			// All of folder is the folder, leave as is
		} else {
			*s = '\0';
		}
		// Now, check to make sure this folder is shared, if not, share it!
		getShareName(folder, SAFESTR(share));
		// Then, build the new path, append it to the list
		if (s) {
			sprintf(newpath, "%s%s\\%s", hostname, share, s+1);
		} else {
			sprintf(newpath, "%s%s", hostname, share);
		}
		strcatf(newGameDataDirs, "%s\n", newpath);
	}
	// Write the full thing to a file
	srand(time(NULL));
	sprintf(tempfile, "c:\\temp\\gamedatadir_%d.txt", rand() % 256);
	file = fopen(tempfile, "w");
	fwrite(newGameDataDirs, 1, strlen(newGameDataDirs), file);
	fclose(file);

	// Copy to the Xbox
	{
		char xbcppath[2048];
		size_t retsize;
		getenv_s(&retsize, SAFESTR(xbcppath), "XEDK");
		strcat(xbcppath, "\\bin\\win32\\xbcp");

		sprintf(cmd, "\"%s\" /y %s %s DEVKIT:\\%s\\gamedatadir.txt", xbcppath, extraParams, tempfile, projectName);
		ret = system(cmd);
		if (ret) {
			printf("Error returned from xbcp\n");
		}
	}
	fileForceRemove(tempfile);

	return ret;
}
