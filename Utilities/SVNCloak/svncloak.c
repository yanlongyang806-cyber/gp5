#include <stdio.h>
#include "wininclude.h"
#include "file.h"
#include "utils.h"
#include "assert.h"
#include "FolderCache.h"
#include "gimmeDLLWrapper.h"

int lineCount(char *s)
{
	int ret =0;
	while (*s)
		if (*s++=='\n')
			ret++;
	return ret;
}

int main(int argc, char **argv)
{
	char *s, *folderpath;
	char svnpath[MAX_PATH];
	char svnpathout[MAX_PATH];
	char *data;
	char *nextchunk;
	char chunk[100000];
	char folder[MAX_PATH];
	FILE *fout;

	DO_AUTO_RUNS;
	DO_AUTO_RUNS_FILE;

	fileAllPathsAbsolute(true);
	FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);
	gimmeDLLDisable(true);

	if (argc!=2) {
		printf("Usage: %s <folder_name>\n", argv[0]);
		return 1;
	}

	{
		char message[10000];
		sprintf(message, "This will set SVN to no longer update this folder *and* delete it from the local disk.  Are you sure you want to do this?");
		if (IDYES != MessageBox(NULL, message, "SVNCloak Confirmation", MB_YESNO))
			return 0;
	}

	folderpath = argv[1]; 
	forwardSlashes(folderpath);
	if (folderpath[strlen(folderpath)-1] == '/')
		folderpath[strlen(folderpath)-1] = '\0';
	// folderpath = c:/src/junk/dircopy

	strcpy(svnpath, folderpath);
	s = strrchr(svnpath, '/');
	assert(s);
	strcpy_unsafe(s, "/.svn/entries"); // svnpath = c:/src/junk/.svn/entries

	strcpy(folder, strrchr(folderpath, '/') + 1); 
	strcat(folder, "\n"); // folder = dircopy\n

	data = fileAlloc(svnpath, NULL);
	assert(strStartsWith(data, "10\n"));

	changeFileExt(svnpath, ".out", svnpathout);
	fout = fopen(svnpathout, "wb");

	// Get first chunk
	nextchunk = strchr(data, 0x0c);
	assert(nextchunk);
	memcpy(chunk, data, nextchunk - data);
	chunk[nextchunk - data] = '\0';
	while (lineCount(chunk) < 35)
	{
		if (lineCount(chunk) == 34)
		{
			strcat(chunk, "empty\n");
		} else {
			strcat(chunk, "\n");
		}
	}

	// Write first chunk
	fprintf(fout, "%s%c\n", chunk, 0x0c);

	while (nextchunk)
	{
		char *chunkstart = nextchunk + 2;
		int chunklen;
		char *entryname;
		char *entrytype;
		nextchunk = strchr(chunkstart, 0x0c);
		if (!nextchunk)
			chunklen = (int)strlen(chunkstart);
		else
			chunklen = nextchunk - chunkstart;
		if (chunklen == 0) {
			break;
		}
		memcpy(chunk, chunkstart, chunklen);
		chunk[chunklen] = '\0';
		entryname = chunk;
		entrytype = strchr(chunk, '\n') + 1;
		if (strStartsWith(entrytype, "dir"))
		{
			assert(lineCount(chunk) == 2);
			if (strStartsWith(chunk, folder))
			{
				// Remove it
			} else {
				fprintf(fout, "%s%c\n", chunk, 0x0c);
			}
		} else {
			// Just dump it
			fprintf(fout, "%s%c\n", chunk, 0x0c);
		}
	}
	fclose(fout);

	fileRenameToBak(svnpath);
	rename(svnpathout, svnpath);

	fileMoveToRecycleBin(folderpath);

	return 0;
}