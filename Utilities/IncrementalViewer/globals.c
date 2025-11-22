#include "gimmeDLLWrapper.h"
#include "file.h"
#include "utils.h"

char *requestPath = "N:/incremental/requests";
char *masterRequestListPath = "N:/incremental/master";
char *masterRequestLockPath = "N:/incremental/master_lock";


int LockMasterRequestList()
{
	FILE *lockFile = fopen(masterRequestLockPath, "rt");
	char who[256];
	if ( !lockFile )
	{
		// create a lock file so others will know who is using this file
		lockFile = fopen(masterRequestLockPath, "wt");
		if ( !lockFile )
		{
			printfColor( COLOR_RED|COLOR_BRIGHT, "Error locking masterlist file\n" );
			return 0;
		}
		strcpy(who, gimmeDLLQueryUserName());
		fwrite(who, 1, strlen(who), lockFile);
		fclose(lockFile);
	}
	else
	{
		fgets( who, 256, lockFile );
		printfColor( COLOR_RED|COLOR_BRIGHT, "Could not access %s for writing, it is locked by %s\n", who );
		return 0;
	}
	return 1;
}


int UnlockMasterRequestList()
{
	FILE *lockFile = fopen(masterRequestLockPath, "rt");
	char who[256];

	if ( !lockFile )
	{
		printfColor( COLOR_RED|COLOR_BRIGHT, "Error unlocking Master Request List: it's not locked!" );
		return 0;
	}
	else
	{
		fgets( who, 256, lockFile );

		if ( stricmp(who, gimmeDLLQueryUserName()) == 0 )
		{
			fclose(lockFile);
			// remove the lock on the file
			remove(masterRequestLockPath);
		}
		else
		{
			fclose(lockFile);
			printfColor(COLOR_RED|COLOR_BRIGHT, "Error unlocking Master Request List: it is not locked by you, %s has it locked", who);
			return 0;
		}
	}
	return 1;
}