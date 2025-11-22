#include <stdio.h>
#include <conio.h>
#include "cmdparse.h"
#include "sysutil.h"
#include "systemspecs.h"
#include "UnitSpec.h"
#include "crypt.h"
#include "rand.h"
#include "EString.h"
#include "systemspecs.h"
#include "utilitiesLib.h"
#include "wininclude.h"
#include "resource.h"
#include "ListView.h"
#include "trivia.h"
#include "file.h"
#include <ShlObj.h>
#include "UTF8.h"

static int threads=0;
AUTO_CMD_INT(threads, threads) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

static int noGUI;
AUTO_CMD_INT(noGUI, noGUI) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

static const U32 data_buf_size=32*1024*1024;
void *getDataBuf(void)
{
	U32 i;
	U8 *data_buf;
	data_buf = malloc(data_buf_size);
	for (i=0; i<data_buf_size; i++)
	{
		data_buf[i] = randInt(256);
	}
	return data_buf;
}

// If you change this, consider changing PerfLogger.c also.
__forceinline void localAdler32CalculateCRC(const U8 *input, unsigned int length, U8 *hash)
{
	static const unsigned long BASE = 65521;

	unsigned long s1 = 1;
	unsigned long s2 = 0;
	U16 m_s1, m_s2;

	if (length % 8 != 0)
	{
		do
		{
			s1 += *input++;
			s2 += s1;
			length--;
		} while (length % 8 != 0);

		if (s1 >= BASE)
			s1 -= BASE;
		s2 %= BASE;
	}

	while (length > 0)
	{
		s1 += input[0]; s2 += s1;
		s1 += input[1]; s2 += s1;
		s1 += input[2]; s2 += s1;
		s1 += input[3]; s2 += s1;
		s1 += input[4]; s2 += s1;
		s1 += input[5]; s2 += s1;
		s1 += input[6]; s2 += s1;
		s1 += input[7]; s2 += s1;

		length -= 8;
		input += 8;

		if (s1 >= BASE)
			s1 -= BASE;
		if (length % 0x8000 == 0)
			s2 %= BASE;
	}

	assert(s1 < BASE);
	assert(s2 < BASE);

	m_s1 = (U16)s1;
	m_s2 = (U16)s2;

	hash[3] = (U8)(m_s1);
	hash[2] = (U8)(m_s1 >> 8);
	hash[1] = (U8)(m_s2);
	hash[0] = (U8)(m_s2 >> 8);
}

// If you change this, consider changing PerfLogger.c also.
static F32 adlerSpeedTest(void *data_buf, int timer, U32 size, F32 maxtime)
{
	int i;
	F32 ret;
	U32 hash = 0;
	timerStart(timer);
	for (i=0; i<1500000 && timerElapsed(timer) < maxtime; i++)
		localAdler32CalculateCRC(data_buf, size, (U8*)&hash);
	ret = size * (F32)i / timerElapsed(timer);
	return ret / (1024*1024);
}

F32 RAMSpeedTest2(void *data_buf, int timer, void *data_buf2, F32 maxtime)
{
	int count=0;
	F32 ret;

	memcpy(data_buf2, data_buf, data_buf_size);
	timerStart(timer);
	while (count < 3 && timerElapsed(timer)<maxtime)
	{
		memcpy(data_buf2, data_buf, data_buf_size);
		count++;
	}

	ret = data_buf_size*(F32)count / timerElapsed(timer) * (1.f/(1024*1024));
	return ret;
}


F32 multiSpeedTest(void *data_buf, int timer, void *data_buf2, F32 results[4])
{
	F32 ret=0;
	ret = results[0] = RAMSpeedTest2(data_buf, timer, data_buf2, 0.25);
	ret += results[1] = adlerSpeedTest(data_buf, timer, 1*1024, 0.25);
	ret += results[2] = adlerSpeedTest(data_buf, timer, 1*1024*1024, 0.25);
	ret += results[3] = adlerSpeedTest(data_buf, timer, 32*1024*1024, 0.25);
	return ret/4.f/1000.f;
}

static volatile int thread_go[128];
static volatile int thread_done[128];
static volatile F32 thread_result[128];
void __cdecl threadTestFunc(void *param)
{
	int index = (int)(intptr_t)param;
	void *data_buf = getDataBuf();
	void *data_buf2 = malloc(data_buf_size);
	int timer = timerAlloc();
	F32 saved=0;
	F32 results[4];
	if (system_specs.numRealCPUs < system_specs.numVirtualCPUs)
		SetThreadAffinityMask(GetCurrentThread(), 1 << (index*2 % system_specs.numVirtualCPUs));
	else
		SetThreadAffinityMask(GetCurrentThread(), 1 << (index % system_specs.numRealCPUs));
	while (true)
	{
		if (!saved)
			saved = multiSpeedTest(data_buf, timer, data_buf2, results);
		if (thread_go[index])
		{
			thread_go[index] = 0;
			thread_result[index] = saved;
			saved = 0;
			MemoryBarrier();
			thread_done[index] = 1;
		} else {
			Sleep(1);
		}
	}
}

char *lines[1024];
int numlines=0;
int insertline=0;
int maxlines=ARRAY_SIZE(lines);
int scrollLine=0;
CRITICAL_SECTION csLines;

void setScrollLine(void)
{
	scrollLine = insertline;
}

#define outputLine(...) outputLineEx(true, __VA_ARGS__)

void outputLineEx(bool bPrintf, const char *fmt, ...)
{
	va_list va;
	char buf[4096]={0};

	va_start(va, fmt);
	vsprintf(buf, fmt, va);
	va_end(va);

	if (bPrintf)
		printf("\n%s", buf);
	EnterCriticalSection(&csLines);
	if (insertline == maxlines)
	{
		SAFE_FREE(lines[scrollLine]);
		memmove(&lines[scrollLine], &lines[scrollLine+1], (maxlines - scrollLine - 1)*sizeof(lines[0]));
		insertline--;
	} else {
		assert(insertline < ARRAY_SIZE(lines)-1);
		SAFE_FREE(lines[insertline]);
	}
	assert(insertline < ARRAY_SIZE(lines)-1);
	lines[insertline] = strdup(buf);
	insertline++;
	MAX1(numlines, insertline);
	LeaveCriticalSection(&csLines);
}

void updateLastLine(const char *fmt, ...)
{
	va_list va;
	char buf[4096]={0};

	va_start(va, fmt);
	vsprintf(buf, fmt, va);
	va_end(va);

	printf("\r%s              ", buf);
	EnterCriticalSection(&csLines);
	SAFE_FREE(lines[insertline-1]);
	lines[insertline-1] = strdup(buf);
	LeaveCriticalSection(&csLines);
}

AUTO_STRUCT;
typedef struct LineDesc
{
	char *text; AST(FORMAT_LVWIDTH(255))
} LineDesc;
#include "AutoGen/main_c_ast.c"

volatile enum {
	STATE_FIRSTTEST,
	STATE_PRESSCONTINUE,
	STATE_LOOPINGTESTS,
} g_state = STATE_FIRSTTEST;
HANDLE hUIThreadReady;
ListView *lv;
const char *clipbuf=NULL;

LRESULT CALLBACK DialogMain(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
		//SetWindowText( hDlg, g_RequestStringCaption );
		//SetWindowText(GetDlgItem(hDlg, IDC_EDIT_REQUEST_STRING), g_RequestStringText);
		lv = listViewCreate();
		listViewInit(lv, parse_LineDesc, hDlg, GetDlgItem(hDlg, IDC_LIST1));
		listViewSetColumnWidth(lv, 0, 1000);
		SetTimer(hDlg, 0, 100, NULL);

		SetDlgItemText(hDlg, IDOK, L"Continue");
		EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
		SetDlgItemText(hDlg, IDCANCEL, L"Cancel");
		EnableWindow(GetDlgItem(hDlg, IDCANCEL), TRUE);
		EnableWindow(GetDlgItem(hDlg, IDC_COPYTOCLIPBOARD), FALSE);

		SetEvent(hUIThreadReady);
		break;
	case WM_TIMER:
		EnterCriticalSection(&csLines);
		{
			static LineDesc last_lines[ARRAY_SIZE(lines)];
			static int last_numlines;
			static int last_state = -1;
			int i;
			for (i=0; i<last_numlines; i++)
			{
				if (strcmp(last_lines[i].text, lines[i])!=0)
				{
					SAFE_FREE(last_lines[i].text);
					last_lines[i].text = strdup(lines[i]);
					listViewItemChanged(lv, &last_lines[i]);
				}
			}
			for (i=last_numlines; i<numlines; i++)
			{
				assert(i<ARRAY_SIZE(last_lines)-1);
				last_lines[i].text = strdup(lines[i]);
				listViewAddItem(lv, &last_lines[i]);
			}
			last_numlines = numlines;
			if (last_state != g_state)
			{
				if (g_state == STATE_PRESSCONTINUE)
				{
					SetDlgItemText(hDlg, IDOK, L"Continue");
					EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
					SetDlgItemText(hDlg, IDCANCEL, L"Exit");
					EnableWindow(GetDlgItem(hDlg, IDCANCEL), TRUE);
					EnableWindow(GetDlgItem(hDlg, IDC_COPYTOCLIPBOARD), TRUE);
				}
				last_state = g_state;
			}
		}
		LeaveCriticalSection(&csLines);
		break;
	case WM_COMMAND:
		if ( LOWORD(wParam) == IDOK )
		{
			if (g_state == STATE_PRESSCONTINUE)
			{
				g_state = STATE_LOOPINGTESTS;
				SetDlgItemText(hDlg, IDOK, L"Continue");
				EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
				SetDlgItemText(hDlg, IDCANCEL, L"Exit");
				EnableWindow(GetDlgItem(hDlg, IDCANCEL), TRUE);
			}
			return TRUE;
		}
		if ( LOWORD(wParam) == IDCANCEL )
		{
			EndDialog(hDlg, IDCANCEL);
			exit(0);
			return TRUE;
		}
		if ( LOWORD(wParam) == IDC_COPYTOCLIPBOARD )
		{
			if (clipbuf)
				winCopyToClipboard(clipbuf);
			return TRUE;
		}
		break;
	case WM_DESTROY:
	case WM_CLOSE:
		EndDialog(hDlg, IDCANCEL);
		exit(0);
		return TRUE;
		break;
	}

	return FALSE;
}

void __cdecl UIThread(void *param)
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_MAIN), compatibleGetConsoleWindow(), DialogMain);
}

void startUIThread(void)
{
	InitializeCriticalSection(&csLines);
	hUIThreadReady = CreateEvent(NULL, FALSE, FALSE, NULL);
	_beginthread(UIThread, 0, NULL);
	WaitForSingleObject(hUIThreadReady, INFINITE);
}

void addOtherInfo(char **buf)
{
	TriviaData **trivia;
	estrAppend2(buf, "\n");
	systemSpecsUpdateString();
	trivia = triviaGlobalGet();
	FOR_EACH_IN_EARRAY(trivia, TriviaData, t)
	{
		estrConcatf(buf, "%s\t\"%s\"\n", t->pKey, t->pVal);
	}
	FOR_EACH_END;

	triviaGlobalRelease();
}

//int wmain(int argc, WCHAR** argv_wide)
int __stdcall wWinMain( __in HINSTANCE hInstance, __in_opt HINSTANCE hPrevInstance, WCHAR*    pWideCmdLine, __in int nShowCmd)
{
	F32 speed;
	F32 score=0;
	char *buf=NULL;
	U32 i;
	int run;
	F32 running_avg;
	void *data_buf;
	void *data_buf2;
	int timer;
	F32 base_multicore_score = 0.0;
	bool bUpdateLast=false;
	int loopstartIndex;
	int errorDisplayIndex=-1;
	int overheatCount=0;
	int loopTimer=-1;
	char *lpCmdLine = UTF16_to_UTF8_CommandLine(pWideCmdLine);


	setCavemanMode();

	DO_AUTO_RUNS;

	if (strEndsWith(getExecutableName(), "NoGUI.exe"))
		noGUI = 1;

	{
		char *args[1000];
		char **argv = args;
		int argc;
		args[0] = getExecutableName();
		argc = 1 + tokenize_line_quoted_safe(lpCmdLine,&args[1],ARRAY_SIZE(args)-1,0);
		cmdParseCommandLine(argc, argv);
	}

	if (!noGUI)
		startUIThread();
	else
		newConsoleWindow();

	outputLine("CPU Perf Test");
	outputLine("");

	outputLine("Initializing... ");
	timer = timerAlloc();
	data_buf = getDataBuf();
	data_buf2 = malloc(data_buf_size);
	systemSpecsInit();
	updateLastLine("Initializing... done.");
	bUpdateLast = true;

	{
		char systemspecs_buf[4096];
		char *s;
		char *context=NULL;
		sprintf(systemspecs_buf, "CPU: %0.2fGhz (%d/%d cores) Cache:%0.1fMB\n", system_specs.CPUSpeed / 1000000000.f, system_specs.numRealCPUs, system_specs.numVirtualCPUs, system_specs.cpuCacheSize / (1024.f*1024.f));
		strcatf(systemspecs_buf, "%s\n", system_specs.cpuIdentifier);
		strcatf(systemspecs_buf, "OS: %d.%d.%d\n", system_specs.highVersion, system_specs.lowVersion, system_specs.build);
		estrConcatf(&buf, "%s\n", systemspecs_buf);
		s = strtok_s(systemspecs_buf, "\n", &context);
		while (s)
		{
			if (bUpdateLast)
			{
				updateLastLine(s);
				bUpdateLast = false;
			} else {
				outputLine(s);
			}
			s = strtok_s(NULL, "\n", &context);
		}
	}
	outputLine("");

	outputLine("RAM copy test... ");
	score += speed = RAMSpeedTest2(data_buf, timer, data_buf2, 0.25);
	estrConcatf(&buf, "RAM: %1.1f\n", speed);
	updateLastLine("RAM copy test: %1.2fMB/s", speed);

	outputLine("1K CRC test... ");
	score += speed = adlerSpeedTest(data_buf, timer, 1*1024, 0.25);
	estrConcatf(&buf, "CRC1K: %1.1f\n", speed);
	updateLastLine("1K CRC test: %1.2fMB/s", speed);

	outputLine("1M CRC test... ");
	score += speed = adlerSpeedTest(data_buf, timer, 1*1024*1024, 0.25);
	estrConcatf(&buf, "CRC1M: %1.1f\n", speed);
	updateLastLine("1M CRC test: %1.2fMB/s", speed);

	outputLine("32M CRC test... ");
	score += speed = adlerSpeedTest(data_buf, timer, 32*1024*1024, 0.25);
	estrConcatf(&buf, "CRC32M: %1.1f\n", speed);
	updateLastLine("32M CRC test: %1.2fMB/s", speed);

	estrConcatf(&buf, "Score: %1.2f\n", score/4.f/1000.f);
	outputLine("Single-core score: %1.2f", score/4.f/1000.f);
	outputLine("");

	if (threads)
		system_specs.numRealCPUs = threads;

	outputLine("Testing on %d threads/cores...", system_specs.numRealCPUs);
	printf("\n");

	setScrollLine();
	loopstartIndex = insertline;
	maxlines = insertline+11;
	for (i=0; i<(U32)system_specs.numRealCPUs; i++)
	{
		_beginthread(threadTestFunc, 0, (void*)(intptr_t)i);
	}

	run = 0;
	running_avg = 0;
	while (!_kbhit() || (run >= 0 && run < 4))
	{
		bool bContinue;
		F32 avg;
		char buf2[1024];
		for (i=0; i<(U32)system_specs.numRealCPUs; i++)
		{
			thread_done[i] = 0;
			MemoryBarrier();
			thread_go[i] = 1;
		}
		Sleep(100);
		while (true)
		{
			bContinue = false;
			for (i=0; i<(U32)system_specs.numRealCPUs; i++)
				if (!thread_done[i])
					bContinue = true;
			if (!bContinue)
				break;
			Sleep(1);
		}
		strcpy(buf2, "  ");
		printf("  ");
		avg = 0;
		for (i=0; i<(U32)system_specs.numRealCPUs; i++)
			avg += thread_result[i];
		avg /= system_specs.numRealCPUs;
		if (run == 0)
			base_multicore_score = avg;

		if (avg < 0.80 * base_multicore_score)
		{
			consoleSetFGColor(COLOR_RED|COLOR_BRIGHT);
			printf("     Average: %5.2f    PROBABLY OVERHEATING  (", avg);
			strcatf(buf2, "     Average: %5.2f    PROBABLY OVERHEATING  (", avg);
			overheatCount++;
		} else {
			printf("     Average: %5.2f    ", avg);
			consoleSetFGColor(COLOR_BRIGHT);
			printf("(");
			strcatf(buf2, "     Average: %5.2f    (", avg);
		}
		for (i=0; i<(U32)system_specs.numRealCPUs; i++)
		{
			printf("  %5.2f", thread_result[i]);
			strcatf(buf2, "  %5.2f", thread_result[i]);
		}
		printf(")\n");
		strcat(buf2, ")");
		consoleSetFGColor(COLOR_RED|COLOR_GREEN|COLOR_BLUE);
		outputLineEx(false, "%s", buf2);
		run++;

		if (errorDisplayIndex!=-1)
		{
			static bool bDoneOnce=false;
			int savedline = insertline;
			F32 t = timerElapsed(loopTimer);
			insertline = errorDisplayIndex;
			if (overheatCount)
			{
				outputLine("PROBABLE CPU OVERHEATING %d TIMES (%d:%02d elapsed)",
					overheatCount,
					(int)t / 60,
					(int)t % 60);
			} else {
				outputLine("No errors found (%d:%02d elapsed)",
					(int)t / 60,
					(int)t % 60);
			}
			if (t > 6*60 && !bDoneOnce)
			{
				bDoneOnce = true;
				outputLine("The test has run a sufficiently long enough time to find most CPU overheating");
				outputLine("  issues, you may close this at any time to stop stress testing the CPU.");
			}
			insertline = savedline;
		}

		running_avg += avg;
		if (run == 4)
		{
			running_avg /= 4.f;
			outputLine("Average per-core score: %5.2f", running_avg);
			estrConcatf(&buf, "PerCore: %5.2f\n", running_avg);

			addOtherInfo(&buf);

			outputLine("");

			{
				char *pPath = NULL;

				char outpath[MAX_PATH];
				FILE *fout;
				verify(SHGetSpecialFolderPath_UTF8(NULL, &pPath, CSIDL_DESKTOP, TRUE));
				sprintf(outpath, "%s/CrypticPerfTest.txt", pPath);
				estrDestroy(&pPath);
				fout = fopen(outpath, "w");
				if (!fout)
				{
					outputLine("ERROR: Unable to open \"%s\" for writing.", outpath);
				} else {
					fwrite(buf, 1, strlen(buf), fout);
					fclose(fout);
				}
			}
			if (noGUI)
			{
				winCopyToClipboard(buf);
				outputLine("Results and system specs copied to clipboard and written to CrypticPerfTest.txt on the desktop.");
				outputLine("Press any key to continue multi-core/overheating test, or close this window\n to exit...\n");
				i=_getch();
				while (_kbhit())
					i=_getch();
				printf("Testing on %d threads/cores indefinitely... (press any key to exit)\n", system_specs.numRealCPUs);
			} else {
				clipbuf = buf;
				outputLine("Results and system specs written to CrypticPerfTest.txt on the desktop.");
				outputLine("Press Copy to Clipboard to copy the results to the clipboard.");
				outputLine("Press Continue to continue multi-core/overheating test, or close this window to exit.");
				outputLine("  The multi-core/overheating/stress test  will continue indefinitely and will warn if it detects");
				outputLine("  overheating.  If it detects no problems within 5 minutes it probably will not find any.");
				g_state = STATE_PRESSCONTINUE;
				while (g_state == STATE_PRESSCONTINUE)
					Sleep(1);
				printf("Testing on %d threads/cores indefinitely...\n", system_specs.numRealCPUs);
				insertline = loopstartIndex + 4;
				outputLine("Multi-core/overheating/stress test running...");
				outputLine("This test will run indefinitely and report any errors.");
				errorDisplayIndex = insertline;
				loopTimer = timerAlloc();
				outputLine("No errors found.");
				outputLine("");
				outputLine("");
				outputLine("");
				outputLine("");

				insertline = loopstartIndex;
				maxlines = insertline + 4;
			}
		}
	}
	if (noGUI)
	{
		while (_kbhit())
			i=_getch();

		printf("Press any key to exit...\n");
		i=_getch();
	}

	return 0;
}