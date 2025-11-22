#include "FileListWriter.h"
#include "assert.h"
#include "tokenizer.h"
#include "utils.h"
#include "SourceParser.h"


extern char gFileEndString[];

extern char gVersionString[];

FileListWriter::FileListWriter()
{
	m_pOutFile = NULL;
}

FileListWriter::~FileListWriter()
{
	if (m_pOutFile)
	{
		fclose(m_pOutFile);
	}
}

void FileListWriter::OpenFile(char *pListFileName, char *pObjFileDirName)
{
	char SVNBranch[1024] = "";

	if (m_pOutFile)
	{
		fclose(m_pOutFile);
	}

	m_pOutFile = fopen_nofail(pListFileName, "wt");

	SYSTEMTIME sysTime;

	GetSystemTime(&sysTime);

	fprintf(m_pOutFile, "//This is a list of all files scanned by StructParser at a given date/time, used for dependency purposes\n%d/%d/%d %d:%d:%d:%d\n",
		sysTime.wMonth,
		sysTime.wDay,
		sysTime.wYear,
		sysTime.wHour,
		sysTime.wMinute,
		sysTime.wSecond,
		sysTime.wMilliseconds);

	fprintf(m_pOutFile, "VERSION \"%s\"\n", gVersionString);

	GetSVNVersionAndBranch(pListFileName, SVNBranch, true);

	STATICASSERTF(SVNBranch[0], "Unable to find SVNBranch for %s", pListFileName);

	fprintf(m_pOutFile, "SVNBRANCH \"%s\"\n", SVNBranch);

	fprintf(m_pOutFile, "\"%s\"\n", pObjFileDirName);
}
void FileListWriter::AddFile(char *pFileName, int iExtraData, int iNumDependencies, int iDependencies[MAX_DEPENDENCIES_SINGLE_FILE])
{
	STATICASSERT(m_pOutFile != NULL, "File list file not open");

	fprintf(m_pOutFile, "\"%s\"\n%d\n%d\n", pFileName, iExtraData, iNumDependencies);

	if (iNumDependencies)
	{
		int i;

		for (i=0; i < iNumDependencies; i++)
		{
			fprintf(m_pOutFile, "%d ", iDependencies[i]);
		}

		fprintf(m_pOutFile, "\n");
	}
}

void FileListWriter::CloseFile()
{
	fprintf(m_pOutFile, gFileEndString);

	if (m_pOutFile)
	{
		fclose(m_pOutFile);
	}

	m_pOutFile = NULL;
}
