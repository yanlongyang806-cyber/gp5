#include "launchdialog.h"
#include "strings_opt.h"


int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
	char updater_cmdline[1024] = {0};
	

	do
	{
		if ( IDOK != DialogBox(NULL, (LPCTSTR)(IDD_DIALOG1), NULL, LaunchDlgProc) )
			return 0;

		if ( !updater[0] )
		{
			MessageBox( NULL, "No Updater specified.  Please specify an updater to run.", "Error", MB_OK );
			return 0;
		}

	} while ( !updater[0] );

	// execute the updater with the parameters given
	{
		PROCESS_INFORMATION pi = {0};
		STARTUPINFO si = {0};
		si.cb = sizeof(si);

		// make the command line
		STR_COMBINE_BEGIN(updater_cmdline);
			STR_COMBINE_CAT( updater );
			STR_COMBINE_CAT( " " );
			if ( folder[0] )
			{
				STR_COMBINE_CAT( "-folder \"" );
				STR_COMBINE_CAT( folder );
				STR_COMBINE_CAT( "\"" );
			}
			if ( server[0] )
			{
				STR_COMBINE_CAT( " -us " );
				STR_COMBINE_CAT( server );
			}
			if ( noself )
				STR_COMBINE_CAT( " -noself" );
			if ( cov )
				STR_COMBINE_CAT( " -cov" );
			if ( console )
				STR_COMBINE_CAT( " -console" );
			if ( majorpatch )
				STR_COMBINE_CAT( " -mp" );
			if ( test )
				STR_COMBINE_CAT( " -test" );
			if ( nolaunch )
				STR_COMBINE_CAT( " -nolaunch" );
		STR_COMBINE_END();

		//launch updater with given parameters
		if ( !CreateProcess(updater, updater_cmdline, NULL, NULL, FALSE, 
			CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi) )
		{
			int err;
			err = GetLastError();
			MessageBox( NULL, "Error launching updater", "ERROR", MB_OK|MB_ICONERROR );
			exit( err );
		}
	}

	return 0;
}