#include <stdtypes.h>
#include "sysutil.h"

FILE * gpFile;
#define MAX_INPUT 4096
char gszInput[ MAX_INPUT ];
char gszInputBackup[ MAX_INPUT ];

int gbWin32Config = 0;
int gbXboxConfig = 0;
char gszConfig[ MAX_INPUT ];
char gszXboxConfig[ MAX_INPUT ];

int GetNextLine()
{
	size_t inputLength;

	if ( fgets( gszInput, MAX_INPUT, gpFile ) == NULL )
		return 0;

	inputLength = strlen( gszInput );
	if ( inputLength && gszInput[ inputLength - 1 ] == '\n' )
	{
		ANALYSIS_ASSUME(inputLength > 0);
//#pragma warning(suppress:6386) // /analyze 
		gszInput[ inputLength - 1 ] = '\0';
	}

	return 1;
}

char * FindToken()
{
	char * pszToken = gszInput;
	while ( *pszToken && isspace((unsigned char)*pszToken ) )
		++pszToken;

	return pszToken;
}

int ConsumeInput()
{
	return GetNextLine();
}

int AddXboxConfig( const char * pszConfigLine )
{
	strcat( gszXboxConfig, pszConfigLine );
	strcat( gszXboxConfig, "\n" );
	return 1;
}

int AddWin32Config( const char * pszConfigLine )
{
	strcat( gszConfig, pszConfigLine );
	strcat( gszConfig, "\n" );
	return 1;
}

int AddConfig( const char * pszConfigLine )
{
	return AddXboxConfig( pszConfigLine ) && AddWin32Config( pszConfigLine );
}

int ParseConfigName()
{
	char * pszConfigName = FindToken();
	gbWin32Config = !strcmp( pszConfigName, "Name=\"Debug|Win32\"" );
	gbXboxConfig = !strcmp( pszConfigName, "Name=\"Profile|Xbox 360\"" );

	AddConfig( gszInputBackup );
	if ( gbWin32Config )
	{
		AddWin32Config( gszInput );

		// change to xbox360 profile
		strcpy_s( pszConfigName, MAX_INPUT - ( pszConfigName - gszInput ), "Name=\"Profile|Xbox 360\"" );
		AddXboxConfig( gszInput );
	}
	else
		AddConfig( gszInput );


	return 1;
}

int ParseConfigContents()
{
	char * pToken = FindToken();
	int retCode = strcmp( pToken, "</FileConfiguration>" );
	if ( retCode )
	{
		if ( gbWin32Config && !strcmp( pToken, "Name=\"VCCLCompilerTool\"" ) )
		{
			AddWin32Config( gszInput );

			// change the tool setting for the xbox
			strcpy_s( pToken, MAX_INPUT - ( pToken - gszInput ), "Name=\"VCCLX360CompilerTool\"" );
			AddXboxConfig( gszInput );
		}
		else
		if ( !gbXboxConfig )
			AddConfig( gszInput );
	}
	else
	if ( !gbXboxConfig )
		AddConfig( gszInput );

	return ConsumeInput() && retCode;
}


int ParseFileConfig()
{
	const char * pToken = FindToken();
	if (pToken)
	{
		ANALYSIS_ASSUME(pToken);
		if (strncmp( pToken, "<FileConfiguration", 18 ) == 0)
		{
			gszConfig[ 0 ] = '\0';
			gszXboxConfig[ 0 ] = '\0';

			strcpy( gszInputBackup, gszInput );
			if ( !ConsumeInput() || !ParseConfigName() || !ConsumeInput() )
				return 0;

			while ( ParseConfigContents() )
			{
			}

			// print the config unmodified, except for xbox profile - in which case we'll use the modified 
			// Win32 "debug" profile
			if ( !gbXboxConfig )
				printf( "%s", gszConfig );

			if ( gbWin32Config )
				printf( "%s", gszXboxConfig );

			return 1;
		}
	}

	return 0;
}

int ParseDefault()
{
	printf( "%s\n", gszInput );
	return ConsumeInput();
}

int ParseMain()
{
	return ParseFileConfig() || ParseDefault();
}


int SyncProjConfig()
{
	if ( !GetNextLine() )
		return 0;

	while ( ParseMain() )
	{
	}

	return 0;
}

int wmain(int argc, const S16* argv_wide[])
{
	errno_t err;
	char **argv;
	ARGV_WIDE_TO_ARGV
	DO_AUTO_RUNS

	if ( argc != 2 )
	{
		fprintf( stderr, "Usage: SyncProjConfig FILENAME.VCPROJ\n" );
		return -1;
	}

	err = fopen_s( &gpFile, argv[ 1 ], "r" );
	if ( err != 0 || !gpFile )
	{
		fprintf( stderr, "Failed to open %s\n", argv[ 1 ] );
		return -2;
	}

	SyncProjConfig( gpFile );

	fclose( gpFile );

	return 0;
}

