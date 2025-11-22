#include "dataValidation.h"
#include "ui.h"
#include "hoglib.h"
#include "utils.h"
#include "fileutil2.h"
#include "earray.h"
#include "estring.h"
#include <psapi.h>
#include "UTF8.h"

#define BOOLEAN_TEXT(a) ((a) ? "true" : "false")

static void appendManifestTrivia(const char *filename, const char *dataFilename, char **estrOutput)
{
	int len = 0;
	char *manifestText = fileAlloc(filename, &len);
	char *baseName = NULL;
	if(manifestText)
	{
		char seps[] = "\r\n";
		char *context = NULL;
		char *line;

		line = strtok_s(manifestText, seps, &context);
		while(line)
		{
			if(strstri(line, dataFilename))
			{
				// If it harvests the wrong line, it's because something stupid was passed in as the filename (eg. a directory name)
				estrConcatf(estrOutput, "[%s] %s\n", filename, line);
				break;
			}

			line = strtok_s(NULL, seps, &context);
		}

		free(manifestText);
	}
}

static void appendHoggTrivia(const char *filename, const char *dataFilename, char **estrOutput, bool *bGoodChecksum)
{
	HogFile *hogFile = hogFileRead(filename, NULL, PIGERR_PRINTF, NULL, HOG_READONLY|HOG_NOCREATE|HOG_NO_REPAIR);
	if(hogFile)
	{
		HogFileIndex index = hogFileFind(hogFile, dataFilename);
		if(index != HOG_INVALID_INDEX)
		{
			U32 timestamp  = hogFileGetFileTimestamp(hogFile, index);
			U32 fileSize   = hogFileGetFileSize(hogFile, index);
			U32 checksum   = hogFileGetFileChecksum(hogFile, index);
			U64 offset     = hogFileGetOffset(hogFile, index);
			bool isGood    = hogFileChecksumIsGood(hogFile, index);
			bool isZipped  = hogFileIsZipped(hogFile, index);
			bool isSpecial = hogFileIsSpecialFile(hogFile, index);

			*bGoodChecksum = isGood;

			estrConcatf(estrOutput, "[%s] [%s] TIME:%u SIZE:%u SUM:%i OFFSET:%"FORM_LL"u GOOD:%s ZIPPED:%s SPECIAL:%s\n", 
				filename,
				dataFilename,
				timestamp,
				fileSize,
				checksum,
				offset,
				BOOLEAN_TEXT(isGood),
				BOOLEAN_TEXT(isZipped),
				BOOLEAN_TEXT(isSpecial)
			);
		}
		hogFileDestroy(hogFile, true);
	}
}

static void appendPatchxferTrivia(const char *startPath, const char *dataFilename, char **estrOutput)
{
	char path[MAX_PATH], filename[MAX_PATH], line[1024], today[16];
	FILE *logfile=NULL;
	struct tm nowtm;
	__time64_t now;
	char *lastLine = NULL;

	strcpy(path, startPath);
	while(path[0])
	{
		sprintf(filename, "%s/patchxfer.log", path);
		logfile = fopen(filename, "r");
		if(logfile)
			break;

		if(strlen(path)==3 && path[1] == ':')
			break; // Path looks like "C:\", bail out
		getDirectoryName(path);

	}
	if(!logfile)
	{
		LogError("DV: Unable to find patchxfer.log");
		return;
	}

	_time64(&now);
	_gmtime64_s(&nowtm, &now);
	strftime(today, ARRAY_SIZE_CHECKED(today), "%y%m%d", &nowtm);
	estrStackCreate(&lastLine);
	estrCopy2(&lastLine, "");
	while(fgets(line, ARRAY_SIZE_CHECKED(line), logfile))
	{
		if(strStartsWith(line, today) && strstri(line, dataFilename))
		{
			estrPrintf(&lastLine, "[%s] %s\n", filename, line);			
		}
	}
	fclose(logfile);
	if (lastLine && *lastLine)
		estrConcatf(estrOutput, "%s", lastLine);
	estrDestroy(&lastLine);
}

bool appendDataValidationTrivia(HANDLE hProcess, const char *dataDir, const char *dataFilename, char **estrOutput, bool *bAppendFilename)
{
	char *pBasePath = NULL;
	char *pBasePathDir = NULL;

	char *tempFilePath = NULL;
	char *tempTrivia = NULL;
	char **filenames = NULL;
	bool bAppendFilenameIfChecksumGood = false;
	bool bAllChecksumsGood = true;
	
	if (strstri(*estrOutput, "ValidationError:AppendFilename"))
		bAppendFilenameIfChecksumGood = true;

	estrCopy2(&tempTrivia, "");

	if (dataDir)
	{
		estrCopy2(&pBasePath, dataDir);
	}
	else if(GetModuleFileNameEx_UTF8(hProcess, NULL, &pBasePath))
	{
		
	}
	else
	{
		estrDestroy(&pBasePath);
		LogError("DV: Couldn't detect process name");
		return false;
	}

	estrGetDirAndFileName(pBasePath, &pBasePathDir, NULL);
	estrDestroy(&pBasePath);


	if(strstri(dataFilename, "data/") == dataFilename)
	{
		dataFilename += 5; // Advance past "data/" if it is in the front
	}

	// -------------------------------------------------------------------------
	// Find and scan all manifests

	estrPrintf(&tempFilePath, "%s/.patch", pBasePathDir);
	filenames = fileScanDirNoSubdirRecurse(tempFilePath);
	if(filenames)
	{
		EARRAY_FOREACH_BEGIN(filenames, i);
		{
			if(strstri(filenames[i], ".manifest"))
			{
				appendManifestTrivia(filenames[i], dataFilename, &tempTrivia);
			}
		}
		EARRAY_FOREACH_END;

		fileScanDirFreeNames(filenames);
	}

	// -------------------------------------------------------------------------
	// Find and scan all hoggs

	estrPrintf(&tempFilePath, "%s/piggs", pBasePathDir);
	filenames = fileScanDirNoSubdirRecurse(tempFilePath);
	if(filenames)
	{
		EARRAY_FOREACH_BEGIN(filenames, i);
		{
			if(strstri(filenames[i], ".hogg"))
			{
				bool bGoodChecksum;
				appendHoggTrivia(filenames[i], dataFilename, &tempTrivia, &bGoodChecksum);
				if (!bGoodChecksum)
					bAllChecksumsGood = false;
			}
		}
		EARRAY_FOREACH_END;

		fileScanDirFreeNames(filenames);
	}

	if (bAllChecksumsGood && bAppendFilenameIfChecksumGood)
		*bAppendFilename = true;

	// -------------------------------------------------------------------------
	// Scan for patchxfer.log

	appendPatchxferTrivia(pBasePathDir, dataFilename, &tempTrivia);

	// -------------------------------------------------------------------------
	// Append a single, escaped trivia string and cleanup

	estrConcatf(estrOutput, "ValidationError:Data ");
	estrAppendEscaped(estrOutput, tempTrivia);
	estrConcatf(estrOutput, "\n");

	estrDestroy(&tempFilePath);
	estrDestroy(&tempTrivia);
	estrDestroy(&pBasePathDir);
	return true;
}
