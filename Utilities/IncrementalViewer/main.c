#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "utils.h"
#include "EArray.h"
#include "incrementalrequest.h"
#include "requestmanager.h"

#pragma comment (lib, "../../libs/utilitieslib/UtilitiesLib_md.lib")

int WINAPI WinMain ( HINSTANCE hInstance, HINSTANCE hPrevInstance, 
					LPSTR lpCmdLine, int nCmdShow )
{
	char *argv[1000];
	int argc, i = 0;
	int requesting_files = 0;
	StringArray files;
	char *comment = NULL;

	argc = tokenize_line_quoted(lpCmdLine, &argv[0], 0);
	newConsoleWindow();

	EArrayCreate(&files);

	for( i = 0; i < argc; ++i )
	{
		if ( stricmp(argv[i], "-request") == 0 ||
			stricmp(argv[i], "-rq") == 0 ||
			stricmp(argv[i], "-r") == 0 )
		{
			++i;
			if ( *argv[i] == '-' )
			{
				printfColor(COLOR_RED|COLOR_BRIGHT, "File Request List is empty.");
				continue;
			}
			requesting_files = 1;
			while( *argv[i+1] != '-' )
			{
				EArrayPush(&files, argv[i]);
				++i;
			}
			EArrayPush(&files, argv[i]);
		}
		else if ( stricmp(argv[i], "-comment") == 0 )
		{
			++i;
			if ( *argv[i] == '-' )
			{
				printfColor(COLOR_RED|COLOR_BRIGHT, "Comment field is empty.");
				continue;
			}
			comment = strdup(argv[i]);
		}
	}

	if ( requesting_files )
	{
		CreateRequest( files, EArrayGetSize(&files), comment );
	}
	else
	{
		if ( CompileMasterRequestList() )
			RunRequestManager();
	}

	EArrayDestroy(&files);
	return 0;
}