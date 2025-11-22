#include "errortrackerhandler.h"
#include "autogen/ErrorTrackerHandlerInfo_h_ast.h"
#include "file.h"
#include "cmdparse.h"
#include "sysutil.h"
#include "utils.h"
#include "textparser.h"
#include "wininclude.h"
#include "winutil.h"
#include "resource.h"
#include "fileWatch.h"
#include "../../3rdparty/zlib/zlib.h"
#include "UTF8.h"

#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

#define MASTER_SOLUTION_DIR "c:\\src\\MasterSolution"
#define MASTER_SOLUTION "c:\\src\\MasterSolution\\AllProjectsMasterSolution.sln"
#define MASTER_SOLUTION_HAS_VS2010 "c:\\src\\MasterSolution\\AllProjectsMasterSolution_vs2005.sln" // just for checking existence
#define VS2010_PATH "C:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\Common7\\IDE\\devenv.exe"
#define IS_VS2010 isVS2010()
//#define MASTER_SOLUTION (IS_VS2010?MASTER_SOLUTION_VS2010:MASTER_SOLUTION_VS2005)

#define MAX_WBITS_AND_USE_GZIP_PLEASE (MAX_WBITS + 16)

#define WM_ET_TEXT            (WM_USER+1)
#define WM_ET_PROGRESS        (WM_USER+2)
#define WM_ET_SUCCESS         (WM_USER+3)
#define WM_ET_FAILURE         (WM_USER+4)
#define WM_ET_CANCEL_COMPLETE (WM_USER+5)

static void parseCmdLine(char *lpCmdLine);
static void CenterWindow(HWND hwnd);
INT_PTR CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);

ErrorTrackerHandlerInfo sInfo    = {0};
char sDumpShortName[MAX_PATH]    = {0};
char sDumpFilename[MAX_PATH]     = {0};
char sSolutionFilename[MAX_PATH] = {0};

static HWND shDlg = INVALID_HANDLE_VALUE;
static bool sbCancel = false;
static bool sbComplete = false;

// --------------------------------------------------------------------------------

// let's just be dumb about this for now (see parseCmdLine)
static char sHandlerFile[MAX_PATH] = {0};
//AUTO_CMD_SENTENCE(sHandlerFile, file);

bool isVS2010(void)
{
	static int cached=-1;
	if (cached == -1)
	{
		int newcached=0;
		if (fileSize(VS2010_PATH)) // VS2010 is installed (can probably skip this check now that we're looking at .sln versions - a VS2010 solution won't work in VS2005 anyway)
		{
			int len;
			char *sln = fileAlloc(MASTER_SOLUTION, &len);
			if (sln) // APMS should always exist
			{
				char *s = strstr(sln, "Format Version ");
				if (s)
				{
					float ver;
					if (sscanf(s, "Format Version %f", &ver)==1)
					{
						if (ver >= 11.0)
							newcached = 1;
					}
				}
				fileFree(sln);
			}
		}
		cached = newcached;
	}
	return !!cached;
}

// --------------------------------------------------------------------------------

int APIENTRY wWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     WCHAR*    pWideCmdLine,
					 int       nCmdShow)
{
	char *lpCmdLine = UTF16_to_UTF8_CommandLine(pWideCmdLine);

	DO_AUTO_RUNS

	fileWatchSetDisabled(true);

	if(strlen(lpCmdLine) == 0)
	{
		winRegisterMe("open", ".eth");
		return 0;
	}

	parseCmdLine((char*)lpCmdLine);

	InitCommonControls();
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAINDLG), NULL, MainDlgProc);
	return 0;
}

// --------------------------------------------------------------------------------

static void parseCmdLine(char *lpCmdLine)
{
	// --------------------------------------------------------------------------------
	// The fancy auto_cmd way ... we're going to be a bit dumber for this one

	//int argc = 0, oldargc;
	//char *args[1000];
	//char **argv = args;
	//char buf[1000]={0};

	//args[0] = getExecutableName();
	//oldargc = 1 + tokenize_line_quoted_safe(buf,&args[1],ARRAY_SIZE(args)-1,0);
	//argc = oldargc + tokenize_line_quoted_safe(lpCmdLine,&args[oldargc],ARRAY_SIZE(args)-oldargc,0);
	//cmdParseCommandLine(argc, argv);
	// --------------------------------------------------------------------------------
	int len;
	if(*lpCmdLine == '\"')
		lpCmdLine++;

	strcpy_s(SAFESTR(sHandlerFile), lpCmdLine);

	len = (int)strlen(sHandlerFile);
	if(len > 0)
	{
		if(sHandlerFile[len-1] == '\"')
			sHandlerFile[len-1] = 0;
	}
}

// --------------------------------------------------------------------------------

static void outputfv(const char *format, va_list args)
{
	static char *buffer = NULL;
}

static void outputf(const char *format, ...)
{
	static char *buffer = NULL;
	estrClear(&buffer);

	VA_START(args, format);
	estrConcatfv(&buffer, format, args);
	VA_END();

	SendMessage(shDlg, WM_ET_TEXT, 0, (LPARAM)buffer);
}

static void set_progress(int perc)
{
	SendMessage(shDlg, WM_ET_PROGRESS, 0, (LPARAM)perc);
}


// --------------------------------------------------------------------------------
// Solution generation

static bool generateSolution(void)
{
	FILE *f_solution_in  = NULL;
	FILE *f_solution_out = NULL;
	char line[1024];
	bool bAddedDump = false;

	if(sDumpFilename[0] == 0)
		return false;

	sprintf_s(SAFESTR(sSolutionFilename), "%s\\CrashDump.sln", MASTER_SOLUTION_DIR);

	f_solution_in  = fopen(MASTER_SOLUTION, "rt");
	if(!f_solution_in)
	{
		outputf("ERROR: failed to open %s!", MASTER_SOLUTION);
		sSolutionFilename[0] = 0;
		return false;
	}

	f_solution_out = fopen(sSolutionFilename, "wt");
	if(!f_solution_out)
	{
		fclose(f_solution_in);

		outputf("ERROR: failed to open %s!", sSolutionFilename);
		sSolutionFilename[0] = 0;
		return false;
	}

	if (IS_VS2010)
		bAddedDump = true; // Adding the .mdmp to the .sln doesn't work for VS2010

	while(fgets(line, 1024, f_solution_in))
	{
		if(!bAddedDump)
		{
			char *pThisProjectGUID = strstri(line, "Project(\"");
			if(pThisProjectGUID)
			{
				char guid[1024];
				char *end;

				pThisProjectGUID += strlen("Project(\"");
				end = strstri(pThisProjectGUID, "\")");

				if(end)
				{
					int len = (int)(end - pThisProjectGUID);
					char addedProjFormat[1024];
					strncpy_s(SAFESTR(guid), pThisProjectGUID, len);
					guid[len] = 0;

					outputf("Found Project GUID: %s", guid);

					sprintf_s(SAFESTR(addedProjFormat), 
						"Project(\"%s\") = \"%s\", \"%s\", \"{9A81DF59-B601-40C5-A134-B4B1A9901769}\"\nEndProject\n",
						"{8BC9CEB9-9B4A-11D0-8D11-00A0C91BC942}",//guid,
						"CrashDump",
						sDumpFilename);

					fwrite(addedProjFormat, 1, strlen(addedProjFormat), f_solution_out);
					bAddedDump = true;
				}
			}
		}

		fwrite(line, 1, strlen(line), f_solution_out);
	}

	fclose(f_solution_in);
	fclose(f_solution_out);

	return true;
}

// --------------------------------------------------------------------------------
// Visual Studio execution (taken from Jimb's code, modified to use ShellExecute)

static bool launchVS(const char *solutionPath, const char *dumpPath)
{
	char *paths[] = {
		VS2010_PATH,
		"C:\\Program Files\\Microsoft Visual Studio 8\\Common7\\IDE\\devenv.exe",
		"C:\\Program Files (x86)\\Microsoft Visual Studio 8\\Common7\\IDE\\devenv.exe",
	};

	int index=-1;
	int i;
	char buf[1024];

	for (i=0; i<sizeof(paths)/sizeof(paths[0]); i++) {
		if (i==0 && !IS_VS2010)
			continue;
		if (fileSize(paths[i])>0) {
			index = i;
			break;
		}
	}
	if (index==-1)
		return false;

	if (IS_VS2010)
	{
		sprintf(buf, "%s %s", solutionPath, dumpPath);
	} else {
		sprintf(buf, "%s /run", solutionPath);
	}
	ShellExecute_UTF8(NULL, NULL, paths[index], buf, NULL, SW_SHOWNORMAL);
	return true;
}



// --------------------------------------------------------------------------------
// Where the actual work takes place ...

#define FAIL { sbComplete = true; return 0; if(f) { fclose(f); f = NULL; }; SendMessage(shDlg, WM_ET_FAILURE, 0, 0); }

#define INFLATE_BUFSIZE 4096

static DWORD WINAPI WorkerThread(LPVOID lpParam)
{
	char readBuffer[INFLATE_BUFSIZE];
	char  zipBuffer[INFLATE_BUFSIZE];
	int magic = 0;
	int len = 0;
	int version = 0;
	bool bSuccess = false;

	int amt_read;
	char *text = NULL;
	FILE *f = NULL;
	FILE *dumpfile = NULL;

	// --------------------------------------------------
	// Open up the file, do some basic checks

	outputf("Opening %s...", sHandlerFile);
	f = fopen(sHandlerFile, "rb");
	if(!f)
	{
		outputf("ERROR: Failed to open %s!", sHandlerFile);
		FAIL;
	}

	fread(&magic,   4, 1, f);
	fread(&version, 4, 1, f);
	fread(&len,     4, 1, f);

	if(magic != MAGIC_HANDLER_HEADER)
	{
		outputf("ERROR: Invalid header!");
		FAIL;
	}

	if(version != 1)
	{
		outputf("ERROR: Invalid version!");
		FAIL;
	}

	if(len <= 0)
	{
		outputf("ERROR: Invalid text block length!");
		FAIL;
	}

	// --------------------------------------------------
	// Read in the text block and build sInfo

	text = malloc(len+1);

	amt_read = (int)fread(text, 1, len, f);
	if(amt_read != len)
	{
		outputf("ERROR: Truncated text block!");
		FAIL;
	}
	text[len] = 0;

	ParserReadText(text, parse_ErrorTrackerHandlerInfo, &sInfo, 0);

	// --------------------------------------------------
	// Inflate the gzipped block 

	{
		int err;
		int iTotalBytes;
		int iFileSize;
		int iCurrentLoc;
		z_stream z = {0};
		int zflags = 0;

		z.zalloc = 0;
		z.zfree  = 0;

		z.next_in  = readBuffer;
		z.avail_in = 0;

		z.next_out  = zipBuffer;
		z.avail_out = INFLATE_BUFSIZE;

		iCurrentLoc = ftell(f);
		fseek(f, 0, SEEK_END);
		iFileSize   = ftell(f) - iCurrentLoc;
		fseek(f, iCurrentLoc, SEEK_SET);

		iTotalBytes = 0;

		if(iFileSize == 0)
		{
			outputf("ERROR: Invalid dump block!");
			FAIL;
		}

		outputf("Decompressing dump file...");

		err = inflateInit2(&z, MAX_WBITS_AND_USE_GZIP_PLEASE);
		if(err != Z_OK)
		{
			outputf("ERROR: Failed to init zlib!");
			FAIL;
		}

		sprintf_s(SAFESTR(sDumpShortName), "CrashDump.%s", sInfo.bFullDump ? "dmp" : "mdmp");
		sprintf_s(SAFESTR(sDumpFilename), "c:\\temp\\%s", sDumpShortName);
		makeDirectoriesForFile(sDumpFilename);
		dumpfile = fopen(sDumpFilename, "wb");
		if(!dumpfile)
		{
			outputf("ERROR: Failed to open temporary output dump file! (%s)", sDumpFilename);
			FAIL;
		}

		for(;;)
		{
			if(sbCancel)
			{
				break;
			}

			// Fill our read buffer, if necessary
			if(z.avail_in == 0)
			{
				// Read in a bit more of the chunk
				int perc;
				int iReadBytes = (int)fread(readBuffer, 1, INFLATE_BUFSIZE, f);
				if(iReadBytes == 0) // feof()
					zflags |= Z_FINISH;

				iTotalBytes += iReadBytes;

				perc = (int)((F32)iTotalBytes / (F32)iFileSize * 100.0f);
				set_progress(perc);

				z.avail_in = iReadBytes;
				z.next_in  = readBuffer;
			}

			// clear our write buffer, if necessary
			if(z.avail_out == 0)
			{
				fwrite(zipBuffer, 1, INFLATE_BUFSIZE, dumpfile);

				// Reset the zipBuffer ptr
				z.avail_out = INFLATE_BUFSIZE;
				z.next_out  = zipBuffer;
			}

			err = inflate(&z, zflags);

			if(err == Z_STREAM_END)
			{
				int leftover = INFLATE_BUFSIZE - z.avail_out;
				if(leftover > 0)
				{
					fwrite(zipBuffer, 1, leftover, dumpfile);
				}

				break;
			}
			else if(err != Z_OK)
			{
				break;
			}
		}

		fclose(dumpfile);

		inflateEnd(&z);

		if(sbCancel)
		{
			outputf("Operation Cancelled!");
			Sleep(1500);
			SendMessage(shDlg, WM_ET_CANCEL_COMPLETE, 0, 0);
			FAIL;
		}
		else if(err != Z_STREAM_END)
		{
			outputf("ERROR: Failed to decompress properly!");
			FAIL;
		}
	}

	outputf("Generated dump file: %s", sDumpFilename);

	// --------------------------------------------------
	// Solution generation

	outputf("Generating SLN file ...");
	if(generateSolution())
	{
		outputf("Executing VS.NET ...");
		bSuccess = launchVS(sSolutionFilename, sDumpFilename);
	}
	else
	{
		outputf("ERROR: Failed to generate SLN file!");
	}

	// --------------------------------------------------
	// Finish up

	fclose(f);
	f = NULL;

	set_progress(100);
	outputf("Complete!");
	sbComplete = true;

	if(bSuccess)
		SendMessage(shDlg, WM_ET_SUCCESS, 0, 0);

	return 0;
}

// --------------------------------------------------------------------------------

static void InitMainDlg(HWND hDlg)
{
	DWORD id = 0;
	HWND hTemp;

	hTemp = GetDlgItem(shDlg, IDC_PROGRESSBAR);
	SendMessage(hTemp, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
	SendMessage(hTemp, PBM_SETPOS,   0, 0);

	CenterWindow(hDlg);

	shDlg = hDlg;
	CreateThread(NULL, 0, WorkerThread, NULL, 0, &id);
}

static void ShutdownMainDlg(HWND hDlg)
{
	StructDeInit(parse_ErrorTrackerHandlerInfo, &sInfo);
}

// --------------------------------------------------------------------------------

INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
		InitMainDlg(hDlg);
		return (INT_PTR)TRUE;

	case WM_DESTROY:
		ShutdownMainDlg(hDlg);
		return (INT_PTR)TRUE;

	case WM_ET_TEXT:
		{
			HWND hOutput = GetDlgItem(hDlg, IDC_OUTPUT);
			int index = ListBox_AddString_UTF8(hOutput, (char*)lParam);
			SendMessage(hOutput, LB_SETTOPINDEX, index, 0);
		}
		return TRUE;

	case WM_ET_PROGRESS:
		{
			HWND hTemp = GetDlgItem(hDlg, IDC_PROGRESSBAR);
			SendMessage(hTemp, PBM_SETPOS, (int)lParam, 0);
			return TRUE;
		}

	case WM_ET_SUCCESS:
		{
			EndDialog(hDlg, IDCANCEL);
			return TRUE;
		}

	case WM_ET_FAILURE:
		{
			// Do nothing ... let them read the errors.
			return TRUE;
		}

	case WM_ET_CANCEL_COMPLETE:
		{
			EndDialog(hDlg, IDCANCEL);
			return TRUE;
		}

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDCANCEL:
			{
				EndDialog(hDlg, LOWORD(wParam));
				return TRUE;
			}

		case IDC_CANCEL:
			{
				EnableWindow(GetDlgItem(hDlg, IDC_CANCEL), FALSE);
				sbCancel = true;
				return TRUE;
			}
		}
		break;
	}
	return (INT_PTR)FALSE;
}

// --------------------------------------------------------------------------------
// from a CodeProject sample
static void CenterWindow(HWND hwnd)
{
	int x, y;
	HWND hwndDeskTop;
	RECT rcWnd, rcDeskTop;

	hwndDeskTop = GetDesktopWindow();
	GetWindowRect(hwndDeskTop, &rcDeskTop);
	GetWindowRect(hwnd, &rcWnd);

	x = rcDeskTop.right / 2;
	y = rcDeskTop.bottom / 2;
	x -= rcWnd.right / 2;
	y -= rcWnd.bottom / 2;

	SetWindowPos(hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
}

#include "autogen/ErrorTrackerHandlerInfo_h_ast.c"
