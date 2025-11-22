#include "AutoGen/gimmeDLLPublicInterface_h_ast.h"
#include "EString.h"
#include "file.h"
#include "gimme.h"
#include "gimmeBranch.h"
#include "GimmeDLL.h"
#include "gimmeDLLPrivateInterface.h"
#include "gimmeUtil.h"
#include "logging.h"
#include "MemAlloc.h"
#include "patchme.h"
#include "timing.h"
#include "timing_profiler_interface.h"

extern void gimmeDLLStartup(void);

// Name of log file for Gimme API operations.
static const char *gimmeDllLogFileName = "C:\\gimmeDllLog.txt";

// Append GimmeDLL log line.
void GimmeDllLog(const char *pLogLine)
{
	const int maxLogSize = 20*1024*1024;  // 20 MB
	static bool firstRun = true;

	PERFINFO_AUTO_START_FUNC();

	// Rotate log if necessary.
	if (firstRun && fileSize(gimmeDllLogFileName) > maxLogSize)
	{
		HANDLE hMutex = CreateMutex(NULL, FALSE, L"Global\\gimmeDllLog.txt.lock");
		if (fileSize(gimmeDllLogFileName) > maxLogSize)
		{
			char backup[MAX_PATH];
			strcpy(backup, gimmeDllLogFileName);
			strcat(backup, ".bak");
			fileMove(gimmeDllLogFileName, backup);
		}
		ReleaseMutex(hMutex);
	}
	firstRun = false;

	// Write log line.
	filelog_printf(gimmeDllLogFileName, "%s", pLogLine);

	PERFINFO_AUTO_STOP_FUNC();
}

// Log function entry and exit.
static void GimmeTraceLog(bool bEnter, const char *pFunction, U32 uDuration, const char *pFormat, va_list ap)
{
	char format[256];
	char *line = NULL;;

	PERFINFO_AUTO_START_FUNC();

	// Format log line.
	if (bEnter)
		sprintf_s(SAFESTR(format), "Entering %s: %s", pFunction, pFormat);
	else
		sprintf_s(SAFESTR(format), "Leaving %s (%lu): %s", pFunction, uDuration, pFormat);
	estrStackCreate(&line);
	estrConcatfv(&line, pFormat, ap);

	// Write log line.
	GimmeDllLog(line);
	estrDestroy(&line);

	PERFINFO_AUTO_STOP_FUNC();
}

// Gimme has been entered.
#define GimmeEnterf(pFormat, ...) {U32 start; do {PERFINFO_AUTO_START(__FUNCTION__,1); start = timeGetTime(); GimmeEnterf_dbg(__FUNCTION__, FORMAT_STRING_CHECKED(pFormat), __VA_ARGS__);} while(0)
static void GimmeEnterf_dbg(const char *pFunction, FORMAT_STR const char * pFormat, ...)
{
	va_list ap;

	va_start(ap, pFormat);
	//GimmeTraceLog(true, pFunction, 0, pFormat, ap);
	va_end(ap);
}

// Gimme has been left.
#define GimmeLeavef(pFormat, ...) do {U32 duration; ErrorOncePerFrame(); duration = timeGetTime() - start; GimmeLeavef_dbg(__FUNCTION__, duration, FORMAT_STRING_CHECKED(pFormat), __VA_ARGS__); PERFINFO_AUTO_STOP();} while(0);}do {} while(0)
static void GimmeLeavef_dbg(const char *pFunction, U32 uDuration, FORMAT_STR const char * pFormat, ...)
{
	va_list ap;

	va_start(ap, pFormat);
	//GimmeTraceLog(false, pFunction, uDuration, pFormat, ap);
	va_end(ap);
}

GIMMEDLL_API GimmeErrorValue GimmeDoOperation(const char *fullpath, GIMMEOperation op, GimmeQuietBits quiet)
{
	GimmeErrorValue ret;
	gimmeDLLStartup();
	GimmeEnterf("fullpath \"%s\" op %s quiet %x", fullpath, StaticDefineIntRevLookup(GIMMEOperationEnum, op), quiet);

	ret = patchmeDoOperation(fullpath, op, quiet, true, &ret) ? ret : gimmeDoOperation(fullpath, op, quiet);

	GimmeLeavef("ret %d", (int)ret);
	return ret;
}

GIMMEDLL_API GimmeErrorValue GimmeDoOperations(const char **fullpaths, int file_count, GIMMEOperation op, GimmeQuietBits quiet)
{
	GimmeErrorValue ret = GIMME_NO_ERROR;
	int i;
	char *fullpathsString = NULL;
	bool first = true;

	gimmeDLLStartup();

	estrStackCreate(&fullpathsString);
	for (i=0; i<file_count; i++) 
	{
		if (!first)
			estrConcatChar(&fullpathsString, ',');
		first = false;
		estrAppend2(&fullpathsString, fullpaths[i]);
	}
	GimmeEnterf("fullpaths \"%s\" file_count %d op %s quiet %x", fullpathsString, file_count,
		StaticDefineIntRevLookup(GIMMEOperationEnum, op), quiet);
	estrDestroy(&fullpathsString);

	if (!patchmeDoOperationEx(fullpaths, file_count, op, quiet, true, &ret))
	{
		// Hack to support for old gimme DBs, probably never will be used, never tested, but should work ;)
		ret=GIMME_NO_ERROR;
		for (i=0; i<file_count; i++) 
		{
			GimmeErrorValue r2 = gimmeDoOperation(fullpaths[i], op, quiet);
			if (r2)
				ret = r2;
		}
	}
	GimmeLeavef("ret %d", ret);
	return ret;
}

// Set the default checkin comment, when doing a GIMME_CHECKIN with GIMME_QUIET.
GIMMEDLL_API GimmeErrorValue GimmeSetDefaultCheckinComment(const char *comment)
{
	patchmeSetDefaultCheckinComment(comment);
	return GIMME_NO_ERROR;
}

GIMMEDLL_API const char *GimmeQueryIsFileLocked(const char *fullpath)
{
	const char *ret;
	gimmeDLLStartup();
	GimmeEnterf("fullpath \"%s\"", fullpath);

	ret = patchmeQueryIsFileLocked(fullpath, &ret) ? ret : gimmeQueryIsFileLocked(fullpath);

	GimmeLeavef("ret \"%s\"", ret);
	return ret;
}

GIMMEDLL_API const char *GimmeQueryLastAuthor(const char *fullpath)
{
	const char *ret;
	gimmeDLLStartup();
	GimmeEnterf("fullpath \"%s\"", fullpath);

	ret = patchmeQueryLastAuthor(fullpath, &ret) ? ret : gimmeQueryLastAuthor(fullpath);

	GimmeLeavef("ret \"%s\"", ret);
	return ret;
}

GIMMEDLL_API const char *GimmeQueryUserName(void)
{
	const char *ret;
	gimmeDLLStartup();
	GimmeEnterf("");

	ret = gimmeGetUserName();

	GimmeLeavef("ret \"%s\"", ret);
	return ret;
}

GIMMEDLL_API int GimmeQueryAvailable(void)
{
	int ret;
	gimmeDLLStartup();
	GimmeEnterf("");

	ret = patchmeQueryAvailable() && gimmeQueryAvailable();

	GimmeLeavef("ret %d", ret);
	return ret;
}

GIMMEDLL_API const char *GimmeQueryBranchName(const char *localpath)
{
	const char *ret;
	gimmeDLLStartup();
	GimmeEnterf("localpath \"%s\"", localpath);

	ret = patchmeQueryBranchName(localpath, &ret) ? ret : gimmeQueryBranchName(localpath);

	GimmeLeavef("ret \"%s\"", ret);
	return ret;
}

GIMMEDLL_API int GimmeQueryBranchNumber(const char *localpath)
{
	int ret;
	gimmeDLLStartup();
	GimmeEnterf("localpath \"%s\"", localpath);

	ret = patchmeQueryBranchNumber(localpath, &ret) ? ret : gimmeQueryBranchNumber(localpath);

	GimmeLeavef("ret %d", ret);
	return ret;
}

GIMMEDLL_API int GimmeQueryCoreBranchNumForDir(const char *localpath)
{
	int ret;
	gimmeDLLStartup();
	GimmeEnterf("localpath \"%s\"", localpath);

	ret = patchmeQueryCoreBranchNumForDir(localpath, &ret) ? ret : gimmeQueryCoreBranchNumForDir(localpath);

	GimmeLeavef("ret %d", ret);
	return ret;

}

GIMMEDLL_API const char *GimmeGetErrorString(GimmeErrorValue error)
{
	const char *ret;
	gimmeDLLStartup();
	GimmeEnterf("error %s", StaticDefineIntRevLookup(GimmeErrorValueEnum, error));

	ret = gimmeGetErrorString(error);

	GimmeLeavef("ret \"%s\"", ret);
	return ret;
}

GIMMEDLL_API GimmeErrorValue GimmeDoCommand(const char *cmdline)
{
	GimmeErrorValue ret;
	gimmeDLLStartup();
	GimmeEnterf("cmdline \"%s\"", cmdline);

	ret = gimmeDoCommand(cmdline); // patchme hook is in gimmeDoCommandWrapper

	GimmeLeavef("ret %d", (int)ret);
	return ret;

}

GIMMEDLL_API GimmeErrorValue GimmeBlockFile(const char *fullpath, const char *block_string)
{
	GimmeErrorValue ret;
	gimmeDLLStartup();
	GimmeEnterf("fullpath \"%s\" block_string \"%s\"", fullpath, block_string);

	ret = gimmeBlockFile(fullpath, block_string);

	GimmeLeavef("ret %d", (int)ret);
	return ret;
}

GIMMEDLL_API GimmeErrorValue GimmeUnblockFile(const char *fullpath)
{
	GimmeErrorValue ret;
	gimmeDLLStartup();
	GimmeEnterf("fullpath \"%s\"", fullpath);

	ret = gimmeUnblockFile(fullpath);

	GimmeLeavef("ret %d", (int)ret);
	return ret;
}

GIMMEDLL_API int GimmeQueryIsFileBlocked(const char *fullpath)
{
	int ret;
	gimmeDLLStartup();
	GimmeEnterf("fullpath \"%s\"", fullpath);

	ret = gimmeFileIsBlocked(fullpath);

	GimmeLeavef("ret %d", (int)ret);
	return ret;
}

GIMMEDLL_API const char * const * GimmeQueryGroupListForUser(const char *username)
{
	const char *const *ret;
	gimmeDLLStartup();
	GimmeEnterf("");

	ret = gimmeQueryGroupListForUser(username);

	GimmeLeavef("ret %p", ret);
	return ret;
}

GIMMEDLL_API const char * const * GimmeQueryGroupList(void)
{
	const char *const *ret;
	gimmeDLLStartup();
	GimmeEnterf("");

	ret = gimmeQueryGroupList();

	GimmeLeavef("ret %p", ret);
	return ret;
}

GIMMEDLL_API const char * const * GimmeQueryFullGroupList(void)
{
	const char *const *ret;
	gimmeDLLStartup();
	GimmeEnterf("");

	ret = gimmeQueryFullGroupList();

	GimmeLeavef("ret %p", ret);
	return ret;
}

GIMMEDLL_API const char * const * GimmeQueryFullUserList(void)
{
	const char *const *ret;
	gimmeDLLStartup();
	GimmeEnterf("");

	ret = gimmeQueryFullUserList();

	GimmeLeavef("ret %p", ret);
	return ret;
}

// Composite functions

GIMMEDLL_API int GimmeQueryIsFileLockedByMeOrNew(const char *fullpath)
{
	const char *lock;
	const char *lastauthor;
	int ret;
	gimmeDLLStartup();
	GimmeEnterf("fullpath \"%s\"", fullpath);

	lock = GimmeQueryIsFileLocked(fullpath);
	lastauthor = GimmeQueryLastAuthor(fullpath);
	assert(lastauthor);
	if (!lock) // Not locked by anyone
		ret = stricmp(lastauthor, "Not in database") == 0;
	else
		ret = stricmp(lock, GimmeQueryUserName()) == 0;

	GimmeLeavef("ret %d", (int)ret);
	return ret;
}

GIMMEDLL_API int GimmeQueryIsFileLatest(const char *fullpath)
{
	int ret;
	const char *lastauthor;
	gimmeDLLStartup();
	GimmeEnterf("fullpath \"%s\"", fullpath);

	lastauthor = GimmeQueryLastAuthor(fullpath);
	ret = !!stricmp(lastauthor, "You do not have the latest version");

	GimmeLeavef("ret %d", (int)ret);
	return ret;
}

GIMMEDLL_API int GimmeQueryIsFileMine(const char *fullpath)
{
	// returns true if the file is checked out by me, OR not checked out by anyone and I was the last one to edit it, OR not in the database
	int succeed = 1;
	char *lockee;
	char *lastauthor;
	gimmeDLLStartup();
	GimmeEnterf("fullpath \"%s\"", fullpath);

	lockee = strdup(GimmeQueryIsFileLocked(fullpath));
	lastauthor = strdup(GimmeQueryLastAuthor(fullpath));
	if (((lockee && stricmp(lockee, GimmeQueryUserName())!=0) || // Locked by someone else
		(!lockee && stricmp(lastauthor, GimmeQueryUserName())!=0 && stricmp(lastauthor,"Not in database")!=0))) { // Not locked, not mine, not new, and no errors
			succeed = 0;
	}
	free(lockee);
	free(lastauthor);

	GimmeLeavef("ret %d", succeed);
	return succeed;
}

GIMMEDLL_API void GimmeSetVprintfFunc(VprintfFunc func)
{
	setVprintfFunc(func);
}

GIMMEDLL_API void GimmeSetCrashStateFunc(crashStateFunc func)
{
	setCrashState(func);
}

GIMMEDLL_API void GimmeSetMemoryAllocators(CRTMallocFunc m, CRTCallocFunc c, CRTReallocFunc r, CRTFreeFunc f)
{
	setMemoryAllocators(m, c, r, f);
}

GIMMEDLL_API void GimmeSetAutoTimer(const AutoTimerData *data)
{
	autoTimerSet(data);
}

extern bool gimme_force_manifest;
GIMMEDLL_API void GimmeForceManifest(bool force)
{
	gimmeDLLStartup();
	GimmeEnterf("force %d", (int)force);

	gimme_force_manifest = force;

	GimmeLeavef("");
}

extern bool newConsoleDefaultHidden;
GIMMEDLL_API void GimmeCreateConsoleHidden(bool hidden)
{
	gimmeDLLStartup();
	GimmeEnterf("CreateConsoleHidden %d", (int)hidden);

	newConsoleDefaultHidden = hidden;

	GimmeLeavef("");
}


GIMMEDLL_API bool GimmeForceDirtyBit(const char *fullpath)
{
	bool ret;
	gimmeDLLStartup();
	GimmeEnterf("fullpath \"%s\"", fullpath);

	ret = patchmeForceDirtyBit(fullpath);

	GimmeLeavef("ret %d", (int)ret);
	return ret;
}
