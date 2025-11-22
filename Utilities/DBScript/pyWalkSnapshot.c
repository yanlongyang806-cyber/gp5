#define HAVE_SNPRINTF
#undef _DEBUG
#include "Python.h"
#define _DEBUG

#include "pyLib.h"
#include "pyWalkSnapshot.h"
#include "pyLibContainer.h"
#include "tokenstore.h"
#include "file.h"
#include "timing.h"
#include "WalkSnapshot.h"
#include "UTF8.h"

static PyObject *spMainModule = NULL;

static bool OnLoadContainer(Container *con, U32 uContainerModifiedTime)
{
	return pyProcessContainer(con, uContainerModifiedTime);
}

bool pyWalkSnapshot(GlobalType type, char *pyScriptFilename, char *snapshotFilename, char *offlineFilename, int numThreads)
{
	bool ret = true;
	char *pCWD = NULL;

#ifdef _WIN64
	pyLibSetHomeDir("C:/Night/tools/Python/x64");
#else
	pyLibSetHomeDir("C:/Night/tools/Python/x86");
#endif

	GetCurrentDirectory_UTF8(&pCWD);
	pyLibSetScriptDir(pCWD);
	estrDestroy(&pCWD);
	
	if(!pyLibInitialize())
	{
		fprintf(fileGetStderr(), "ERROR: Could not initialize PythonLib!\n");
		return false;
	}

	Py_SetProgramName("DBScript");
	pyInitDBScriptModule(pyScriptFilename, snapshotFilename);

	spMainModule = pyLibLoadScript(pyScriptFilename, PYLIB_MAIN_MODULE);

	if(!spMainModule)
	{
		fprintf(fileGetStderr(), "ERROR: Could not load script file [%s]!\n", pyScriptFilename);
		return false;
	}

	pyLibInitVars();

	if((ret = pyLibContainerInit(spMainModule)))
	{
		int startTime = timeSecondsSince2000();
		int totalTime;

		pyBegin();

		WalkSnapshot(type, snapshotFilename, offlineFilename, numThreads, OnLoadContainer);

		pyEnd();

		totalTime = timeSecondsSince2000() - startTime;
		fprintf(fileGetStderr(), "Completed script in %d seconds.\n", totalTime);
	}

	pyLibContainerShutdown();
	Py_DecRef(spMainModule);
	spMainModule = NULL;
	pyLibFinalize();
	return ret;
}
