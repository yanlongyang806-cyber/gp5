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
#include "Estring.h"
#include "strings_opt.h"
#include "MemoryPool.h"
#include "stdtypes.h"
#include "fileutil2.h"
#include <sys/stat.h>

// #define UNCHANGE

void fixupBootIni(void)
{
	int i;
	int found = 0;
	char line[255];
	char **lines = NULL;
	FILE *boot;

	system("attrib -S -H -R c:\\boot.ini");
	boot = fileOpen("c:\\boot.ini", "r");

	if(!boot)
	{
		devassertmsg(0, "Failed to open boot file to read it.");
		return;
	}

	while(fgets(line, ARRAY_SIZE_CHECKED(line), boot))
	{
#ifdef UNCHANGE
		if(strstri(line, "\"Microsoft Windows XP Professional\"") && strEndsWith(line, "/3gb\n"))
		{
			// Found it
			char *newline = estrCreateFromStr(line);
			estrTrimLeadingAndTrailingWhitespace(&newline);
			estrTruncateAtLastOccurrence(&newline, ' ');
			estrConcatf(&newline, "\n");
			found = 1;
			eaPush(&lines, strdup(newline));
		}
		else
			eaPush(&lines, strdup(line));
#else
		if(strstri(line, "\"Microsoft Windows XP Professional\"") && !strstri(line, "/3gb"))
		{
			// Found it
			char *newline = estrCreateFromStr(line);
			estrTrimLeadingAndTrailingWhitespace(&newline);
			estrConcatf(&newline, " /3gb\n");
			found = 1;
			eaPush(&lines, strdup(newline));
		}
		else
			eaPush(&lines, strdup(line));
#endif
	}

	if(!found)
	{
		system("attrib +S +H +R c:\\boot.ini");
		return;
	}

	fileClose(boot);
	if(fileExists("c:\\boot.ini.bak"))
		system("attrib -S -H -R c:\\boot.ini.bak");
	fileCopy("c:\\boot.ini", "c:\\boot.ini.bak");
	system("attrib +S +H +R c:\\boot.ini.bak");

	boot = fileOpen("c:\\boot.ini", "w");

	if(!boot)
	{
		devassertmsg(0, "Failed to open boot file to rewrite it.");
		return;
	}

	for(i=0; i<eaSize(&lines); i++)
	{
		char *lineOut = lines[i];

		fwrite(lineOut, sizeof(char), strlen(lineOut), boot);
	}
	
	fileClose(boot);
	system("attrib +S +H +R c:\\boot.ini");
}

int main(int argc, char* argv[])
{
	DO_AUTO_RUNS;
    if(IsUsingVista())
    {
        // ab: vista needs bcdedit /set IncreaseUserVa 3072
        //     but this needs admin permission too
    }
    else
    {
        fixupBootIni();
    }
	return 0;
}
