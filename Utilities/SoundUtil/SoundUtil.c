// SoundUtil.c : Defines the entry point for the console application.
//

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "file.h"
#include <ctype.h>
#include "utils.h"
#include "memcheck.h"

#include "fileUtil.h"
#include "StashTable.h"
#include "timing.h"
#include "error.h"
#include "earray.h"
#include "FolderCache.h"
#include "strings_opt.h"
#include "MemoryPool.h"
#include "stdtypes.h"
#include "fileutil2.h"

int g_commonfound = 0;
char *g_commonblock = NULL;
int g_commonblocksize = 0;
char *g_commonfile = NULL;

void findCommonFDP(void)
{
	char **files;
	int count, i;
	files = fileScanDirFolders("c:\\fightclub\\src\\sound", FSF_FILES|FSF_NOHIDDEN);
    count = eaSize( &files );

	for(i=0; i<count; i++)
	{
		char* file = files[i];

		if(strEndsWith(file, "Common.fdp"))
		{
			g_commonfound = 1;
			g_commonfile = strdup(file);
			break;
		}
	}

	fileScanDirFreeNames(files);
}

void extractCommonFSB(void)
{
	char* bankstart, *bankend;
	FILE* file = fopen(g_commonfile, "r");
	char* rebuild;

	int size = fileGetSize(file);

	char* filebuf = calloc(size+1, sizeof(char));

	fread(filebuf, size, 1, file);

	bankstart = strstr(filebuf, "<soundbank>");
	bankend = strstr(bankstart, "</soundbank>");

	g_commonblocksize = (bankend+12) - bankstart;

	g_commonblock = calloc(g_commonblocksize+1, sizeof(char));
	memcpy(g_commonblock, bankstart, g_commonblocksize);
	g_commonblock[g_commonblocksize] = '\0';

	rebuild = strstr(g_commonblock, "<rebuild>");
	rebuild += strlen("<rebuild>");
	rebuild[0] = '0';  // Disable building of the common bank

	free(filebuf);
}

void insertCommonFSBHelper(char* filename)
{
	char* bankstart;
	int prebank, postbank;
	FILE* file = fopen(filename, "r");
	FILE* write;
	int stat = 0;
	int ret;

	int size = fileGetSize(file);

	char* filebuf = calloc(size+1, sizeof(char));

	fread(filebuf, size, 1, file);
	fclose(file);

	// fairly fragile
	if(strstri(filebuf, "<name>Common</name>"))
	{
		return;  // Already there
	}

	printf("Adding common bank to: %s\n", filename);

	write = fopen(filename, "w");

	if(!write)
	{
		stat = ~(_S_IWRITE | _S_IREAD);
		ret = chmod(filename, _S_IREAD | _S_IWRITE);
		write = fopen(filename, "w");
	}

	bankstart = strstr(filebuf, "<soundbank>");
	if(!bankstart)
	{
		bankstart = strstr(filebuf, "</default_soundbank_props>");
		bankstart += strlen("</default_soundbank_props>");
	}
	prebank = bankstart - filebuf;
	postbank = size - prebank;

	fwrite(filebuf, prebank, 1, write);
	fwrite(g_commonblock, g_commonblocksize, 1, write);
	fwrite("\n", 1, 1, write);
	fwrite(bankstart, postbank, 1, write);

	if(stat)
	{
		ret = chmod(filename, stat);
	}

	free(filebuf);
}

void insertCommonFSB(void)
{
	char **files;
	int count, i;
	files = fileScanDirFolders("c:\\fightclub\\src\\sound", FSF_FILES|FSF_NOHIDDEN);
    count = eaSize( &files );

	for(i=0; i<count; i++)
	{
		char* file = files[i];

		if(strEndsWith(file, ".fdp") && !strEndsWith(file, "Common.fdp"))
		{
			consoleSetColor(COLOR_GREEN, 0);
			insertCommonFSBHelper(file);
		}
	}

	fileScanDirFreeNames(files);
}

void deleteCommonFSBHelper(char* filename)
{
	char* bankstart;
	char* commonstart = NULL, *commonend;
	char* namestart;
	FILE* file = fopen(filename, "r");
	FILE* write;
	int prebank, postbank;
	int stat = 0, ret;

	int size = fileGetSize(file);

	char* filebuf = calloc(size+1, sizeof(char));

	fread(filebuf, size, 1, file);
	fclose(file);

	write = fopen(filename, "w");

	if(!write)
	{
		stat = ~(_S_IWRITE | _S_IREAD);
		ret = chmod(filename, _S_IREAD | _S_IWRITE);
		write = fopen(filename, "w");
	}

	bankstart = strstr(filebuf, "<soundbank>");
	while(bankstart)
	{
		namestart = strstr(bankstart, "<name>");
		assert(namestart);
		if(!strnicmp(namestart, "<name>Common</name>", 19))
		{
			commonstart = bankstart;
			break;
		}

		bankstart = strstr(bankstart+11, "<soundbank>");
	}
	if(!commonstart)
	{
		fwrite(filebuf, size, 1, write);
		fclose(write);
		return;
	}
	
	commonend = strstr(commonstart,"</soundbank>");
	assert(commonend);
	commonend += strlen("</soundbank>");

	prebank = commonstart - filebuf;
	postbank = (filebuf + size) - commonend;
	
	fwrite(filebuf, prebank, 1, write);
	fwrite(commonend, postbank, 1, write);

	if(stat)
	{
		ret = chmod(filename, stat);
	}

	free(filebuf);
}

void deleteCommonFSB(void)
{
	char **files;
	int count, i;
	files = fileScanDirFolders("c:\\fightclub\\src\\sound", FSF_FILES|FSF_NOHIDDEN);
    count = eaSize( &files );

	for(i=0; i<count; i++)
	{
		char* file = files[i];

		if(strEndsWith(file, ".fdp") && !strEndsWith(file, "Common.fdp"))
		{
			consoleSetColor(COLOR_GREEN, 0);
			printf("Adding common bank to: %s\n", file);
			consoleSetDefaultColor();
			deleteCommonFSBHelper(file);
		}
	}

	fileScanDirFreeNames(files);
}

void buildFMODFilesHelper(char* filename)
{
	char build[MAX_PATH];
	char *file;
	char designercl[] = "\"C:\\Program Files\\FMOD SoundSystem\\FMOD Designer\\fmod_designercl.exe\"";

	forwardSlashes(filename);
	file = strrchr(filename, '/');
	
	file[0] = '\0';
	file++;

	sprintf(build, "cmd /c start /WAIT /D\"%s\" %s %s -pc %s", filename, designercl, designercl, file);
	sprintf(build, "cmd /c start /WAIT /D\"%s\" %s %s -xbox360 %s", filename, designercl, designercl, file);

	system(build);
}

void buildFMODFiles(void)
{
	char **files;
	int count, i;
	files = fileScanDirFolders("c:\\fightclub\\src\\sound", FSF_FILES|FSF_NOHIDDEN);
    count = eaSize( &files );

	for(i=0; i<count; i++)
	{
		char* file = files[i];

		if(strEndsWith(file, ".fdp") && !strEndsWith(file, "Common.fdp"))
		{
			consoleSetColor(COLOR_GREEN, 0);
			printf("Adding common bank to: %s\n", file);
			buildFMODFilesHelper(file);
		}
	}

	fileScanDirFreeNames(files);
}

int wmain(int argc, WCHAR** argv_wide)
{
	DO_AUTO_RUNS
	consoleSetColor(COLOR_GREEN, 0);
	printf("Hiya MikeJoe!\n");
	consoleSetDefaultColor();

	findCommonFDP();

	if(!g_commonfound)
	{
		consoleSetColor(COLOR_RED, 0);
		printf("FATAL ERROR: Common.fdp not found!");
		exit(-1);
	}
	forwardSlashes(g_commonfile);

	printf("Common FDP: %s\n", g_commonfile);

	extractCommonFSB();

	insertCommonFSB();

	// Build here

	if(!fileExists("C:\\Program Files\\FMOD SoundSystem\\FMOD Designer\\fmod_designercl.exe"))
	{
		consoleSetColor(COLOR_RED, 0);
		printf("C:\\Program Files\\FMOD SoundSystem\\FMOD Designer\\fmod_designercl.exe not found.  Bug Adam?");
	}
	else
	{
		buildFMODFiles();

		consoleSetDefaultColor();
		printf("Builds completed.\n");
	}
	system("pause");
	return 0;
}
