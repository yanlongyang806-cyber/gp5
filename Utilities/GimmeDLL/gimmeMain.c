#include "gimme.h"
#include "gimmeUtil.h"
#include "gimmeUtilPurge.h"
#include "gimmeBranch.h"
#include "gimmeDatabase.h"
#include <sys/types.h>
#include <sys/utime.h>
#include <io.h>
#include <stdio.h>
#include <process.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "utils.h"
#include "file.h"
#include "fileutil.h"
#include "wininclude.h"
#include "winutil.h"
#include <Shellapi.h>
#include <time.h>
#include <conio.h>
#include <WinCon.h>
#include <direct.h>
#include <crtdbg.h>
#include "earray.h"
#include "hoglib.h"
#include "RegistryReader.h"
#include "sysutil.h"
#include "threadedFileCopy.h"
#include "gimmeCheckinVerify.h"
#include "error.h"
#include "logging.h"
#include "strings_opt.h"
#include "network/crypt.h"
#include "genericDialog.h"
#include "fileWatch.h"
#include "EString.h"
#include "UTF8.h"

#include "AppRegCache.h"

#include "patchme.h"

#define NUM_COPY_THREADS 4
#define NUM_COPY_THREADS_VPN 12

static bool gimmeEnvNoPause(void)
{
	size_t ret_size;
	getenv_s( &ret_size, NULL, 0, "GIMME_NO_PAUSE");
	if (ret_size <=1)
		return false;
	return true;
}


static GimmeErrorValue returnCleanupPause(GimmeErrorValue ret)
{
	gimmeDirDatabaseCloseAll();
	printf("\n");
	logWaitForQueueToEmpty();
	if (gimme_state.pause) {
		_flushall();
		gimme_state.nowarn=0;

		if (gimme_state.simulate) {
			gimmeLog(LOG_WARN_LIGHT, "(Note that you can view all of the above messages by looking at C:\\gimme_log.txt)");
		}
		if (!gimme_state.ignore_errors && !gimmeEnvNoPause()) {
			if (gimme_state.delayPause) {
				gimmeSetOption("DelayPause", 1);
			} else {
				gimmeLog(LOG_WARN_LIGHT, "Press any key to continue");
				setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'G', 0xff5050);
				_getch();
			}
		}

		_chdir("C:\\"); // seems to help windows out if we were doing deletions, etc =)
	}
	return ret;
}

int doQueuedActionsReal(void)
{
	int ret=NO_ERROR;
	int do_checkin;
	GimmeQueuedAction *action;
	const char *comments = NULL;
	char *batchInfo = NULL;

	if (eaSize(&gimme_state.queuedActions)==0)
		return NO_ERROR;
	if (gimme_state.simulate) {
		do_checkin=1;
		comments = "dummy comments";
	} else {
		// Do check for not having ran the game if any of these files are later than the time of the last game running!
		do_checkin = gimmeCheckinVerify(gimme_state.queuedActions);
		if (do_checkin) {
			do {
				do_checkin = gimmeDialogCheckin(gimme_state.queuedActions);
				// get the comments
				comments = gimmeDialogCheckinGetComments();
				// get the batch info
				batchInfo = gimmeDialogCheckinGetBatchInfo();
				if ((!comments || !comments[0]) && do_checkin && getCommentLevel(gimme_state.queuedActions[0]->gimme_dir->local_dir)==CM_REQUIRED) {
					MessageBox(NULL, L"Checkin comments are required!", L"Missing checkin comments", MB_ICONWARNING | MB_OK);
				}
			} while (do_checkin && ((!comments || !comments[0]) && getCommentLevel(gimme_state.queuedActions[0]->gimme_dir->local_dir)==CM_REQUIRED));
		}
		if (do_checkin) {
			gimmeCheckinVerifySaveLog(1);
		} else {
			gimmeCheckinVerifySaveLog(0);
		}
	}

	// In theory, all of these warnings have already been displayed
	gimmeDisableWarnOverride(1);
	while (action = (GimmeQueuedAction *)eaPop(&gimme_state.queuedActions)) {
		int newret = NO_ERROR;
		if (do_checkin) {
			gimmeSetBranchConfigRoot(action->gimme_dir->lock_dir);
			if (action->operation == GIMME_DELETE)
				action->operation = GIMME_ACTUALLY_DELETE;
			newret = gimmeDoOperationRelPath(action->gimme_dir, action->relpath, action->operation, action->quiet);
		} // otherwise, just free the data
		if (do_checkin && !gimme_state.simulate) {
			// log the comments
			gimmeLogComments(action->gimme_dir, action->relpath, action->operation, comments);
			// log the batch info
			gimmeLogBatchInfo(action->gimme_dir, action->relpath, action->operation, batchInfo);
		}
		if (newret != NO_ERROR)
			ret = newret;
		free(action->relpath);
		free(action);
	}
	gimmeDisableWarnOverride(0);
	gimmeLogComments(NULL, NULL, 0, NULL);
	return do_checkin?ret:GIMME_ERROR_CANCELED;
}

static time_t parseDate(char *s) {
	int M, D, Y, H, m, sec=0;
	size_t len =strlen(s);
	char *c;
	struct tm curtime;
	// MMDDYYHH:mm[:ss]
	if (len!=11 && len!=14) {
		gimmeLog(LOG_FATAL, "Invalid string length, expected MMDDYYHH:mm or MMDDYYHH:mm:ss");
		return 0;
	}
	for (c=s; *c; strchr("0123456789", *c)?(char*)(*(c++)-='0'):c++); // make an array of ints by subtracting '0' from all numerical characters
	M=s[0]*10+s[1];
	D=s[2]*10+s[3];
	Y=s[4]*10+s[5];
	H=s[6]*10+s[7];
	m=s[9]*10+s[10];
	if (len==14)
		sec=s[12]*10+s[13];
	curtime.tm_hour=H;
	curtime.tm_min=m;
	curtime.tm_mon=M-1;
	curtime.tm_sec=sec;
	curtime.tm_year=Y+100;
	curtime.tm_isdst=-1;
	curtime.tm_mday=D;
	return mktime(&curtime);
}

static int parseDbNum(char *param)
{
	if ( strIsNumeric(param) )
		return atoi(param);
	else {
		char folder_fullpath[MAX_PATH];
		makefullpath(param,folder_fullpath);
		forwardSlashes(folder_fullpath);
		gimmeLoadConfig();
		if (gimmeCheckDisconnected())
			return 0;
		if (isFullPath(param)) {
			GimmeDir *gimme_dir;
			gimme_dir = findGimmeDir(folder_fullpath);
			if (gimme_dir)
				return eaFind(&eaGimmeDirs, gimme_dir);
		}
	}
	return 0;
}

int gimmeDoCommandWrapperInternal(int argc,char **argv)
{
	int put_back=0;
	int i;
	char *fname;
	char *script_name;
	char long_path_name[CRYPTIC_MAX_PATH];
	FILE *script;
	char cmdlinebuf[10240]="gimme ";
	bool just_did_something;
	int reading_script = 0;
	GimmeErrorValue ret=GIMME_NO_ERROR;
	GimmeErrorValue tempret;

#define HANDLE_RET(func) if (NO_ERROR!=(tempret=(func))) { ret = tempret; gimme_state.pause = true; }
#define HANDLE_RET_NOPAUSE(func) if (NO_ERROR!=(tempret=(func))) { ret = tempret; }
	
	ZeroStruct(&gimme_state);

	consoleUpSize(220, 500);

	gimme_state.queuedActionCallback = doQueuedActionsReal;
	gimme_state.no_need_to_init_wsa = 0;
	gimme_state.launch_editor = 1;
	gimme_state.updateRemoteFileCache = 1; // Command line implies to update the remote cache for the next app-invoked caller

	if (argc == 1)
	{
		printf("Usage: gimme [-checkout] <filename>\n");
		printf("Key: n=database number (0=data, 1=src, etc), b=branch number, [] means optional\n");
		printf("       gimme -put <filename>\n");
		printf("       gimme -editor \"notepad.exe\" <filename>\n");
		printf("       gimme -showall         - show all my locked files\n");
		printf("       gimme -putall          - put all my locked files and all new files\n");
		printf("       gimme -showevery       - show everybody's locked files\n");
		printf("       gimme -getlatest [n]   - get the latest version of all files\n");
		printf("       gimme -getapproved [n] - get the approved/safe version of all files\n");
		printf("       gimme -getbydate MMDDYYHH:mm[:ss] [n|folder] - get based on date\n");
		printf("       gimme -getallbydate MMDDYYHH:mm[:ss]  - get data/ and tools/ based on date\n");
		printf("       gimme -approve [n|folder]     - approve the current version (of database #n)\n");
		printf("       gimme -approvebydate MMDDYYHH:mm[:ss] [n|folder] - approve by timestamp\n");
		printf("       gimme -forceput <filename> - forcefully add to the database (dangerous)\n"); // good for originally adding modified files during the transistional period of getting the system up and running
		printf("       gimme -remove <filename>   - remove a file from the database\n");
		printf("       gimme -glvfile <filename>  - get the latest version of a single file\n");
		printf("       gimme -glvfold <folder> - get the latest version of a folder recursively\n");
		printf("       gimme -glvfold_force <folder> - same as glvfold, but forces gimme to get latest on all files, including ones you have checked out\n");
		printf("       gimme -glvfold_failoncheckedout <folder> - same as glvfold, but fails if it encounters a file you have checked out\n");
		printf("       gimme -diff [diffprog] <filename> - diff a local file with the latest\n");
		printf("                                           revision\n");
		printf("       gimme -checkoutfold <folder>      - check out an entire folder\n");
		printf("       gimme -checkinfold <folder>       - check in an entire folder\n");
		printf("       gimme -forceputfold <folder>      - force check in an entire folder\n");
		printf("       gimme -undofold <folder>          - undoes a check out on an entire folder\n");
		printf("       gimme -checkoutlnks <folder>      - check out all .lnk files in a folder\n"); // Originally Needed for Win2K compat for Steve
		printf("                This is depricated, use '-filespec *.lnk -checkoutfold' instead\n");
		printf("       gimme -rmfold <folder>     - mark an entire folder as being removed\n");
		printf("       gimme -purge <versionfile> - permantly flush a file from the DB (use on N:)\n");
		printf("       gimme -purgeFold <versionfolder> - permantly flush a folder full of files from the DB (use on N:)\n");
		printf("       gimme -stat <filename>     - open Stat window with details on a file\n");
		printf("       gimme -cstat <filename>    - print details on a file to the console\n");
		printf("       gimme -v                   - print gimme.c version number\n");
		printf("       gimme -check [n]           - check the database for consistency vs. the FS\n");
		printf("       gimme -whoami              - displays the name Gimme uses to recognize you\n");
		printf("                        This is either your username or defined by GIMME_USERNAME\n");
		printf("       gimme -undo <filename>     - undoes a checkout\n");
		printf("       gimme -label [n] <Label>   - Adds a LABEL comment into the comment file\n");
		printf("       gimme -commentmode [NONE|ASK|REQUIRED] - sets the persistent mode for comments\n");
		printf("       gimme -register            - add registry settings for Gimme hooks\n");
		printf("       gimme -switchbranch <dir> b  - smoothly change this computer to another branch\n");
		printf("       gimme -setbranchnum <dir> b  - forcibly set which branch this computer is working on\n");
		printf("       gimme -branchstat          - display info on what branches are active\n");
		printf("       gimme -purgebydate MMDDYYHH:mm[:ss] [n] - flush all backups older than specified date (DANGEROUS)\n");
		printf("       gimme -purgebranch n b     - flush all backups in the B branch or older (DANGEROUS)\n");
		printf("       gimme -setverify [0|1]     - sets the registry key indicating to use XCOPY /V\n");
		printf("       gimme -lockfix [-y]        - attempts to fix a locked database [-y to not prompt]\n");
		printf("       gimme -branchreport n b    - reports on files modified after links were broken\n");
// 		printf("       gimme -sync n              - Attempts to repopulate a partial database with your local versions\n");
// 		printf("       gimme -syncnocheckout      - Does not check out writable files when doing a -sync\n");
// 		printf("       gimme -synconlynewer       - Does not check for missing files, only newer ones when -sync'ing\n");
		printf("       gimme -reconnect           - Processes checkouts that happened while disconnected\n");
		printf("       gimme -backupDatabases     - Backup all databases referenced in the config file\n");
		printf("       gimme -relink <filename> <from_branch> [to_branch]	- relinks a file to a previous branch.\n" );
		printf("                   if no \'to_branch\' is specified, it relinks the from_branch to the next highest branch for which there is a revision.\n" );
		printf("\n");
		printf("   Any retrieval command may be prefixed with -nounderscore in order to *exclude*\n");
		printf("    files begining with an underscore (_), which are included by default.\n");
		printf("   Any checkin command may be prefixed with -leavecheckedout in order to leave a\n");
		printf("    file checkedout after updating the system with the local version.\n");
		printf("   Any command may be prefixed with any of the following:\n");
		printf("     -simulate     - \"simulates\" an operation (use \"gimme -simulate -getlatest\"\n");
		printf("                     to see a list of all files that differ between your local\n");
		printf("                     version and the database.\n");
		printf("     -nopause      - disables pausing before exiting (default), except on fatal errors\n");
		printf("     -pause        - forces a pause upon exit.\n");
		printf("     -delayPause   - delays pausing until you call gimme -doDelayPause\n");
		printf("     -doDelayPause - pauses if a previous call to gimem -delayPause had an error needing a pause\n");
		printf("     -ignoreerrors - ignores errors when deciding whether to pause/continue\n");
		printf("     -config file.txt  - changes which config file is used.\n");
		printf("     -quiet        - displays fewer messages.\n");
		printf("     -nowarn       - hide non-fatal warnings.\n");
		printf("     -filespec spec - limit operations to files that match a filespec (e.g. *.lnk)\n");
		printf("     -exfilespec spec - limit operations to files that do not match a filespec (e.g. server/*)\n");
		printf("     -servertime   - display the current server time\n");
		printf("     -servertime val - display the current server time and convert val into a readable format\n");
		printf("     -nodb         - rely only on the filesystem, do not load database.txt/journal.txt\n");
		printf("     -forcedb      - rely only on the db, even for small operations\n");
		printf("     -nocomments   - do not ask for any checkin comments\n");
		printf("     -forceflush   - force writing out the gimme database if there are any changes\n");
		printf("     -noflush      - do not write out the gimme database\n");
		printf("     -overridebranch [n] - override the current branch number for all subsequent commands\n");
		printf("     -nodiff       - does not do a diff (checks in even if no differences occured)\n");
		printf("     -forcezip     - force writing out the gimme database as zipped data\n");
		printf("     -daily        - force check for daily scripts\n");
		printf("     -repeatlastcomment	- require only one comment when checking in multiple files\n");
		printf("     --Rrunthis.bat - runs runthis.bat before executing gimme command\n");
		printf("     -dbstats n outfile [sortcolumn] - outputs statistics for the given database to outfile, optionally sorting by sortcolumn\n");
		printf("     -useCache     - uses the local cache for config files even for stand-alone gimme commands\n");

		return 0;
	}

	gimme_state.num_command_line_operations=0;
	for(i=1;i<argc;i++)
	{
		strcat(cmdlinebuf, argv[i]);
		strcat(cmdlinebuf, " ");
		if (stricmp(argv[i], "-getlatest")==0 || stricmp(argv[i], "-getbydate")==0 || stricmp(argv[i], "-getapproved")==0)
			gimme_state.num_command_line_operations++;
		if (stricmp(argv[i], "-switchbranch")==0)
			gimme_state.num_command_line_operations+=3;
	}
	gimme_state.command_line = cmdlinebuf;
	gimmeUserLog(gimme_state.command_line);
	
	SetConsoleTitle_UTF8(gimme_state.command_line);
	threadedFileCopyInit(NUM_COPY_THREADS);
	hogSetGlobalOpenMode(HogSafeAgainstAppCrash);
	hogSetMaxBufferSize(512*1024*1024);

	for(i=1;i<argc;i++)
	{
		just_did_something=false;
		if (stricmp(argv[i],"-nounderscore")==0)
			gimme_state.no_underscore=1;
		else if (stricmp(argv[i],"-simulate")==0) {
			gimmeLog(LOG_WARN_HEAVY, "[Running in simulation mode]");
			gimme_state.simulate=1;
			gimme_state.pause=1;
		}
		else if (stricmp(argv[i],"-checkout")==0 || stricmp(argv[i], "-get")==0)
			put_back = GIMME_CHECKOUT;
		else if (stricmp(argv[i],"-nopause")==0)
			gimme_state.pause=0;
		else if (stricmp(argv[i],"-pause")==0)
			gimme_state.pause=1;
		else if (stricmp(argv[i],"-delayPause")==0)
			gimme_state.delayPause=1;
		else if (stricmp(argv[i],"-doDelayPause")==0) {
			if (gimmeGetOption("DelayPause")) {
				gimme_state.pause = 1;
				gimmeSetOption("DelayPause", 0);
			}
		} else if (stricmp(argv[i],"-ignoreerrors")==0)
			gimme_state.ignore_errors=1;
		else if (stricmp(argv[i],"-nowarn")==0)
			gimme_state.nowarn=1;
		else if (stricmp(argv[i],"-put")==0)
			put_back = GIMME_CHECKIN;
		else if (stricmp(argv[i],"-checkin")==0)
			put_back = GIMME_CHECKIN;
		else if (stricmp(argv[i],"-script")==0) {
			put_back = GIMME_CHECKIN;
			reading_script = 1;
		} else if (stricmp(argv[i],"-forceput")==0)
			put_back = GIMME_FORCECHECKIN;
		else if (stricmp(argv[i],"-remove")==0)
			put_back = GIMME_DELETE;
		else if (stricmp(argv[i],"-branchstat")==0) {
			gimmeUtilBranchStat();
			gimme_state.pause = 1;
		} else if (stricmp(argv[i],"-editor")==0) {
			gimme_state.editor = argv[i+1];
			i++; just_did_something=true;
		} else if (stricmp(argv[i],"-config")==0) {
			gimme_state.config_file1 = argv[i+1];
			i++; just_did_something=true;
		} else if (stricmp(argv[i],"-quiet")==0) {
			gimme_state.quiet=GIMME_QUIET;
		} else if (stricmp(argv[i],"-threads")==0) {
			int numthreads=0;
			if (argc>i+1 && isdigit(argv[i+1][0])) { // more parameters
				numthreads=atoi(argv[i+1]);
				i++;
			}
			threadedFileCopyInit(numthreads);
		} else if (stricmp(argv[i],"-showevery")==0) {
			int dbnummin=0, dbnummax=-1;
			if (argc>i+1 && isdigit(argv[i+1][0])) { // more parameters
				dbnummin = dbnummax = atoi(argv[i+1]);
				i++;
				if (argc>i+1 && isdigit(argv[i+1][0])) { // more parameters
					dbnummax = atoi(argv[i+1]);
					i++;
				}
				just_did_something=true;
			}
			gimme_state.pause=true;
			HANDLE_RET_NOPAUSE(showLockedFilesByDb(NULL,dbnummin,dbnummax, gimme_state.quiet));
			gimme_state.pause=true;
		} else
		if (stricmp(argv[i],"-showall")==0) {
			int dbnummin=0, dbnummax=-1;
			if (argc>i+1 && isdigit(argv[i+1][0])) { // more parameters
				dbnummin = dbnummax = atoi(argv[i+1]);
				i++;
				if (argc>i+1 && isdigit(argv[i+1][0])) { // more parameters
					dbnummax = atoi(argv[i+1]);
					i++;
				}
				just_did_something=true;
			}
			gimme_state.pause=true;
			HANDLE_RET(showLockedFilesByDb(gimmeGetUserName(),dbnummin,dbnummax,gimme_state.quiet));
		} else
		if (stricmp(argv[i],"-putall")==0) {
			int dbnummin=0, dbnummax=-1;

			gimme_state.doStartFileWatcher = 1;

			if (argc>i+1 && isdigit(argv[i+1][0])) { // more parameters
				dbnummin = dbnummax = atoi(argv[i+1]);
				i++;
				if (argc>i+1 && isdigit(argv[i+1][0])) { // more parameters
					dbnummax = atoi(argv[i+1]);
					i++;
				}
				just_did_something=true;
			}
			gimme_state.pause=true;
			HANDLE_RET(checkinFoldByDb(GIMME_CHECKIN,dbnummin,dbnummax, gimme_state.quiet));
		} else
		if (stricmp(argv[i],"-approve")==0) {
			int dbnum=0;
			if (argc>i+1 && argv[i+1][0]!='-') { // more parameters
				dbnum=parseDbNum(argv[i+1]);
				i++;
				just_did_something=true;
			}
			if (gimme_state.simulate)
				return 0;
			HANDLE_RET(gimmeUtilApprove(dbnum, NULL));
		} else
		if (stricmp(argv[i],"-approvebydate")==0) {
			int dbnum=0;
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no date/time specified for -approvebydate");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			gimme_state.dateToGet = parseDate(argv[i+1]);
			if (gimme_state.dateToGet<=0) {
				gimmeLog(LOG_FATAL, "Error parsing date string \"%s\"", argv[i+1]);
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			if (argc>i+2) { // more parameters
				dbnum=parseDbNum(argv[i+2]);
				i++;
			}
			i++;
			just_did_something=true;
			if (gimme_state.simulate)
				return 0;
			HANDLE_RET(gimmeUtilApprove(dbnum, &gimme_state.dateToGet));
		} else
		if (stricmp(argv[i],"-label")==0) {
			int dbnum=0;
			char label[1024];
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: Database or label specified on -label");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			if (argc>i+1 && argv[i+1][0]!='-') { // more parameters
				dbnum=parseDbNum(argv[i+1]);
				i++;
			}
			if (argc>i+1) {
				strcpy(label, "");
				while (argc>i+1) {
					strcat(label, argv[i+1]);
					strcat(label, " ");
					i++;
				}
			} else {
				strcpy(label, "Label");
			}
			HANDLE_RET(gimmeUtilLabel(dbnum, label));
		} else
		if (stricmp(argv[i],"-getlatest")==0) {
			int dbnum=0;
			gimme_state.doStartFileWatcher = 1;
			gimme_state.cur_command_line_operation++;
			if (argc>i+1 && argv[i+1][0]!='-') { // more parameters
				dbnum=parseDbNum(argv[i+1]);
				just_did_something=true;
				i++;
			}
			HANDLE_RET(gimmeGetLatestVersion(dbnum, gimme_state.quiet));
		} else
		if (stricmp(argv[i],"-getapproved")==0) {
			int dbnum=0;
			gimme_state.doStartFileWatcher = 1;
			if (argc>i+1 && argv[i+1][0]!='-') { // more parameters
				dbnum=parseDbNum(argv[i+1]);
				just_did_something=true;
				i++;
			}
			HANDLE_RET(gimmeGetApprovedVersion(dbnum, gimme_state.quiet));
		} else
		if (stricmp(argv[i],"-getbydate")==0) {
			// MMDDYYHH:mm[:ss]
			int dbnum=0;
			char *folder = NULL;
			gimme_state.doStartFileWatcher = 1;
			gimme_state.cur_command_line_operation++;
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no date/time specified for -getbydate");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			gimme_state.dateToGet = parseDate(argv[i+1]);
			if (gimme_state.dateToGet<=0) {
				gimmeLog(LOG_FATAL, "Error parsing date string \"%s\"", argv[i+1]);
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			if (argc>i+2) { // more parameters
				if ( strIsNumeric(argv[i+2]) )
					dbnum=atoi(argv[i+2]);
				else
					folder=argv[i+2];
				i++;
			}
			else if (argc>i+3) { // more parameters
				dbnum=atoi(argv[i+2]);
				folder=argv[i+3];
				i+=2;
			}
			i++;
			just_did_something=true;
			if ( folder ) {
				HANDLE_RET_NOPAUSE(gimmeGetFolderVersionByTime(dbnum, folder, gimme_state.dateToGet, gimme_state.quiet));
			}
			else {
				HANDLE_RET_NOPAUSE(gimmeGetVersionByTime(dbnum, gimme_state.dateToGet, gimme_state.quiet));
			}
		} else
		if (stricmp(argv[i],"-getallbydate")==0) {
			gimme_state.doStartFileWatcher = 1;
			// MMDDYYHH:mm[:ss]
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no date/time specified for -getbydate");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			gimme_state.dateToGet = parseDate(argv[i+1]);
			if (gimme_state.dateToGet<=0) {
				gimmeLog(LOG_FATAL, "Error parsing date string \"%s\"", argv[i+1]);
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			i++;
			just_did_something=true;
			HANDLE_RET_NOPAUSE(gimmeGetVersionByTime(0, gimme_state.dateToGet, gimme_state.quiet));
			HANDLE_RET_NOPAUSE(gimmeGetVersionByTime(2, gimme_state.dateToGet, gimme_state.quiet));
		} else
		if (stricmp(argv[i], "-purge")==0) {
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no file specified for -purge");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			gimme_state.justDoingPurge = 1;
			makeLongPathName(argv[i+1], long_path_name);
			HANDLE_RET(gimmePurgeFile(long_path_name, 0));
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i], "-purgeFold")==0) {
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no folder specified for -purgeFold");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			makeLongPathName(argv[i+1], long_path_name);
			HANDLE_RET(gimmeUtilPurgeFolder(long_path_name, 0));
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i], "-unpurge")==0) {
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no file specified for -unpurge");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			makeLongPathName(argv[i+1], long_path_name);
			HANDLE_RET(gimmePurgeFile(long_path_name, 1));
			i++;
			just_did_something=true;
		} else
		if(stricmp(argv[i], "-relink")==0)
		{
			int no_to_branch = 0;
			if ( argc == i + 1 )
			{
				gimmeLog(LOG_FATAL, "Error: no file specified for -relink");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			else if ( argc == i + 2 || _tchartodigit(*argv[i+2]) == -1 )
			{
				gimmeLog(LOG_FATAL, "Error: no start branch specified for -relink, or branch was negative");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			no_to_branch = ((argc == i + 3) || _tchartodigit(*argv[i+3]) == -1);
			HANDLE_RET(gimmeRelinkFileToBranch( argv[i+1], atoi(argv[i+2]), 
				no_to_branch ? -1 : atoi(argv[i+3]), gimme_state.quiet));
		} else
		if (stricmp(argv[i], "-diff")==0) {
			if (argc>i+2) {
				i++;
				gimme_state.editor = argv[i];
			} else {
				if (!gimme_state.editor) {
					gimme_state.editor = gimmeDetectDiffProgram();
				}
			}
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no file specified for -diff");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			gimme_state.pause=true;
			makeLongPathName(argv[i+1], long_path_name);
			HANDLE_RET(gimmeUtilDiffFile(long_path_name));
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i], "-cstat")==0) {
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no file specified for -cstat");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			gimme_state.pause=true;
			makeLongPathName(argv[i+1], long_path_name);
			HANDLE_RET(gimmeUtilStatFile(long_path_name, false, true));
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i], "-stat")==0) {
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no file specified for -stat");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
#ifndef _FULLDEBUG
			hideConsoleWindow();
#endif
			makeLongPathName(argv[i+1], long_path_name);
			HANDLE_RET(gimmeUtilStatFile(long_path_name, true, true));
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i],"-glvfile")==0) {
			gimme_state.force_get_latest=true;
			put_back = GIMME_GLV;
		} else
		if (stricmp(argv[i],"-undofold")==0) {
			makeLongPathName(argv[i+1], long_path_name);
			HANDLE_RET_NOPAUSE(undoCheckoutFold(long_path_name, gimme_state.quiet));
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i],"-glvfold")==0) {
			gimme_state.doStartFileWatcher = 1;
			makeLongPathName(argv[i+1], long_path_name);
			HANDLE_RET_NOPAUSE(getLatestVersionFolder(long_path_name, REV_BLEEDINGEDGE, gimme_state.quiet));
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i],"-glvfold_force")==0) {
			gimme_state.doStartFileWatcher = 1;
			gimme_state.force_get_latest=true;
			makeLongPathName(argv[i+1], long_path_name);
			HANDLE_RET_NOPAUSE(getLatestVersionFolder(long_path_name, REV_BLEEDINGEDGE, gimme_state.quiet));
			gimme_state.force_get_latest=false;
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i],"-glvfold_failoncheckedout")==0) {
			gimme_state.doStartFileWatcher = 1;
			gimme_state.fail_on_checkedout=true;
			makeLongPathName(argv[i+1], long_path_name);
			HANDLE_RET_NOPAUSE(getLatestVersionFolder(long_path_name, REV_BLEEDINGEDGE, gimme_state.quiet));
			gimme_state.fail_on_checkedout=false;
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i],"-checkoutfold")==0) {
			gimme_state.doStartFileWatcher = 1;
			makeLongPathName(argv[i+1], long_path_name);
			HANDLE_RET_NOPAUSE(checkoutFold(long_path_name, gimme_state.quiet));
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i],"-checkoutlnks")==0) {
			gimme_state.doStartFileWatcher = 1;
			gimmeAddFilespec("*.lnk", 0);
			makeLongPathName(argv[i+1], long_path_name);
			HANDLE_RET(checkoutFold(long_path_name, gimme_state.quiet));
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i],"-showeveryfold")==0) {
			makeLongPathName(argv[i+1], long_path_name);
			gimme_state.pause=1;
			HANDLE_RET(showLockedFilesFold(long_path_name,gimme_state.quiet, NULL));
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i],"-showallfold")==0) {
			makeLongPathName(argv[i+1], long_path_name);
			gimme_state.pause=1;
			HANDLE_RET(showLockedFilesFold(long_path_name,gimme_state.quiet, gimmeGetUserName()));
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i],"-checkinfold")==0) {
			gimme_state.doStartFileWatcher = 1;
			makeLongPathName(argv[i+1], long_path_name);
			gimme_state.do_extra_checks=1;
			HANDLE_RET_NOPAUSE(checkinFold(long_path_name, gimme_state.quiet));
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i],"-forceputfold")==0) {
			gimme_state.doStartFileWatcher = 1;
			makeLongPathName(argv[i+1], long_path_name);
			HANDLE_RET_NOPAUSE(forcePutFold(long_path_name, gimme_state.quiet));
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i],"-rmfold")==0) {
			gimme_state.doStartFileWatcher = 1;
			makeLongPathName(argv[i+1], long_path_name);
			HANDLE_RET_NOPAUSE(rmFold(long_path_name, gimme_state.quiet));
			i++;
			just_did_something=true;
		} else
		if (stricmp(argv[i],"-v")==0 || stricmp(argv[i],"-version")==0 || stricmp(argv[i], "--version")==0) {
			char *pFileName = NULL;
			GetModuleFileName_UTF8(winGetHInstance(), &pFileName);
			gimmeLog(LOG_FATAL, "GimmeDLL Location: %s", pFileName);
			gimmeLog(LOG_FATAL, "Gimme version: %s", "$Revision: 155762 $");
			gimmeLog(LOG_FATAL, "GimmeDatabase version: %s", gimmeDatabaseGetVersionString());
			gimme_state.pause=1;
			estrDestroy(&pFileName);
		} else
		if (stricmp(argv[i], "-undo")==0) {
			put_back = GIMME_UNDO_CHECKOUT;
		} else
		if (stricmp(argv[i], "-leavecheckedout")==0) {
			gimme_state.leavecheckedout = 1;
		} else
		if (stricmp(argv[i], "-recreate")==0) {
			// (Posslby dangerous?) procedure to re-create database.txt by scanning the directory tree and reading the contents of all .lock files
			int ch;
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no database root passed.  Example: N:\\Revisions\\data\\");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			gimmeLog(LOG_WARN_HEAVY, "[%s] Are you sure you want to do this?  All exact check-in times will be lost [y/N]", argv[i+1]);
			do {
				if (tolower(ch=_getch())=='n' || ch=='\n' || ch=='\r')
					return returnCleanupPause(GIMME_ERROR_CANCELED);
			} while (ch!='y');
			gimmeDatabaseRecreate(argv[i+1]);
			return returnCleanupPause(GIMME_NO_ERROR);
		} else
		if (stricmp(argv[i], "-filespec")==0) {
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no filespec passed");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			i++;
			gimmeAddFilespec(argv[i], 0);
		} else
		if (stricmp(argv[i], "-exfilespec")==0) {
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no filespec passed");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			i++;
			gimmeAddFilespec(argv[i], 1);
		} else
		if (stricmp(argv[i], "-whoami")==0) {
			gimmeLog(LOG_FATAL, "Gimme User Name : %s", gimmeGetUserName());
		} else 
		if (stricmp(argv[i], "-servertime")==0) {
			__time32_t t;
			char buf[128];
			gimmeLoadConfig();
			t = getServerTime(eaGimmeDirs[0]);
			_ctime32_s(SAFESTR(buf), &t);
			gimmeLog(LOG_FATAL, "Current Server Time : %d %s time adjust: %d", t, buf, gimmeGetTimeAdjust());
			if (argc==i+2) {
				t = atoi(argv[i+1]);
				_ctime32_s(SAFESTR(buf), &t);
				gimmeLog(LOG_FATAL, "Time %d == %s", t, buf);
				if (t!=0)
					return 0;
			}
		} else
		if (stricmp(argv[i], "-register")==0) {
			gimmeRegister();
		} else
		if (stricmp(argv[i], "-nodb")==0) {
			gimme_state.db_mode = GIMME_NO_DB;
		} else
		if (stricmp(argv[i], "-forcedb")==0) {
			gimme_state.db_mode = GIMME_FORCE_DB;
		} else
		if (stricmp(argv[i],"-check")==0) {
			int dbnum=0;
			if (argc>i+1 && isdigit(argv[i+1][0])) { // more parameters
				dbnum=atoi(argv[i+1]);
				i++;
			}
			HANDLE_RET(gimmeUtilCheck(dbnum));
			gimme_state.pause = 1;
		} else
		if (stricmp(argv[i], "-daily")==0) {
			// Falls through to generic stuff that happens on every run
		} else
		if (stricmp(argv[i], "-nodaily")==0) {
			// Deprecated
		} else
		if (stricmp(argv[i], "-commentmode")==0) {
			CommentLevel level;
			if (argc==i+1) {
				level = CM_ASK;
			} else {
				i++;
				if (stricmp(argv[i], "none")==0) {
					level = CM_NONE;
				} else if (stricmp(argv[i], "ask")==0) {
					level = CM_ASK;
				} else if (stricmp(argv[i], "required")==0) {
					level = CM_REQUIRED;
				} else {
					gimmeLog(LOG_FATAL, "Invalid parameter to -commentmode specified.  Expected NONE|ASK|REQUIRED");
					return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
				}
			}
			setCommentLevel(level);
		} else
		if (stricmp(argv[i], "-setbranchnum")==0) {
			int number;
			char *root;
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no root directory passed.  Example: -sethbranchnum C:\\game\\data\\ 0");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			i++;
			root = argv[i];
			forwardSlashes(root);
			if (!findGimmeDir(root)) {
				gimmeLog(LOG_FATAL, "Error: the root specified (%s) is not a gimme dir.", root);
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no branch number passed.  Example: -setbranchnum C:\\game\\data\\ 0");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			i++;
			number = atoi(argv[i]);
			gimmeSetBranchNumber(root, number);
		} else
		if (stricmp(argv[i], "-switchbranch")==0) {
			int number;
			char *root;
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no root directory passed.  Example: -switchbranch C:\\game\\data\\ 0");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			i++;
			root = argv[i];
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no branch number passed.  Example: -switchbranch C:\\game\\data\\ 0");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			i++;
			number = atoi(argv[i]);
			// -1 => 0->1, 1->0
			if (number==-1 && gimmeGetBranchNumber(root)>0) {
				number = gimmeGetBranchNumber(root)-1;
			} else if (number==-1) {
				number = 1;
			}
			HANDLE_RET(gimmeUtilSwitchBranch(root, number));
		} else
		if (stricmp(argv[i],"-purgebydate")==0) {
			int dbnum=0;
			gimme_state.pause=1;
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no date/time specified for -purgebydate");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			gimme_state.dateToGet = parseDate(argv[i+1]);
			if (gimme_state.dateToGet<=0) {
				gimmeLog(LOG_FATAL, "Error parsing date string \"%s\"", argv[i+1]);
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			if (argc>i+2) { // more parameters
				dbnum=atoi(argv[i+2]);
				i++;
			}
			i++;
			just_did_something=true;
			gimme_state.pause=1;
			HANDLE_RET(gimmeUtilPurgeByDate(dbnum, gimme_state.dateToGet));
		} else
		if (stricmp(argv[i], "-purgebranch")==0) {
			int number;
			int dbnum;
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no datbase number passed");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			i++;
			dbnum = atoi(argv[i]);
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no branch number passed");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			i++;
			number = atoi(argv[i]);
			HANDLE_RET(gimmeUtilPurgeByBranch(dbnum, number));
		} else
		if (stricmp(argv[i], "-prune")==0) {
			int dbnum;
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no datbase number passed");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			i++;
			dbnum = atoi(argv[i]);
			HANDLE_RET(gimmeUtilPrune(dbnum));
		} else
		if (stricmp(argv[i], "-branchreport")==0) {
			int number;
			int dbnum;
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no datbase number passed");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			i++;
			dbnum = atoi(argv[i]);
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no branch number passed");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			i++;
			number = atoi(argv[i]);
			gimme_state.pause=1;
			HANDLE_RET(gimmeUtilBranchReport(dbnum, number));
		} else
		if (stricmp(argv[i], "-overridebranch")==0) {
			int number, j;
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no branch number passed");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			i++;
			number = atoi(argv[i]);
			if (0!=gimmeSetBranchNumberOverride(number)) {
				gimmeLog(LOG_FATAL, "Error: invalid branch number specified");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			for (j=0; j<eaSize(&eaGimmeDirs); j++) {
				eaGimmeDirs[j]->active_branch = number;
			}
			if (!gimme_state.quiet)
				gimmeLog(LOG_INFO, "Gimme branch number temporarily changed to %d\n", number);
		} else
		if (stricmp(argv[i], "-setverify")==0) {
			int value;
			if (argc==i+1) {
				gimmeLog(LOG_FATAL, "Error: no value passed");
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}
			i++;
			value = atoi(argv[i]);
			gimmeSetOption("Verify", value);

		} else
		if (stricmp(argv[i], "-lockfix")==0) {
			bool auto_unlock=false;
			if (argc==i+2) {
				// Additional parameter
				if (stricmp(argv[i+1], "-y")==0) {
					i++;
					auto_unlock = true;
				}
			}
			HANDLE_RET_NOPAUSE(gimmeUtilLockFix(auto_unlock));
			if (auto_unlock)
				ret = GIMME_NO_ERROR;
		} else
		if (stricmp(argv[i], "-nocomments")==0) {
			setCommentLevelOverride(CM_NONE);
			gimme_state.no_comments = 1;
		} else
		if (stricmp(argv[i], "-noShellExtension")==0) {
			gimme_state.no_shell_extension = 1;
		} else
		if (stricmp(argv[i], "-shellExtensionEvenOnX64")==0) {
			gimme_state.shell_extension_even_on_x64 = 1;
		} else
		if (stricmp(argv[i], "-registerSwitchTo")==0) {
			gimme_state.register_switch_to = 1;
		} else
		if (stricmp(argv[i], "-forceflush")==0) {
			g_force_flush = 1;
		} else
		if (stricmp(argv[i], "-forcezip")==0) {
			g_force_zip = 1;
		} else
		if (stricmp(argv[i], "-noflush")==0) {
			g_force_flush = -1;
			// Also indicates coming from a VPN connection, change threads to a higher amount
			threadedFileCopyInit(NUM_COPY_THREADS_VPN);
		} else
		if (stricmp(argv[i], "-ignorelocks")==0) {
			g_ignore_locks = 1;
		} else
		if (stricmp(argv[i], "-nodiff")==0) {
			gimme_state.ignore_diff = 1;
		} else
		if (stricmp(argv[i],"-repeatlastcomment")==0) {
			gimme_state.repeat_last_comment = 1;
		} else
		if (stricmp(argv[i],"-reconnect")==0) {
			gimmeReconnect();
		} else
		if (stricmp(argv[i],"-backupDatabases")==0) {
			gimmeBackupDatabases();
		} else
// 		if (stricmp(argv[i],"-syncnocheckout")==0) {
// 			gimmeUtilSyncNoCheckout();
// 		} else
// 		if (stricmp(argv[i],"-synconlynewer")==0) {
// 			gimmeUtilSyncOnlyNewer();
// 		} else
// 		if (stricmp(argv[i],"-sync")==0) {
// 			int dbnum=0;
// 			if (argc>i+1 && isdigit(argv[i+1][0])) { // more parameters
// 				dbnum=atoi(argv[i+1]);
// 				i++;
// 				just_did_something=true;
// 			}
// 			if (gimme_state.simulate) return 0;
// 			gimme_state.pause=true;
// 			HANDLE_RET(gimmeUtilSync(dbnum));
// 		} else
		if (stricmp(argv[i],"-block")==0){
			if (argc<=i+2)
				gimmeLog(LOG_FATAL, "Not enough parameters passed to -block");
			else
			{
				gimmeBlockFile(forwardSlashes(argv[i+1]), argv[i+2]);
				i += 2;
			}
		} else
		if (stricmp(argv[i],"-unblock")==0){
			if (argc<=i+1)
				gimmeLog(LOG_FATAL, "Not enough parameters passed to -unblock");
			else
			{
				gimmeUnblockFile(forwardSlashes(argv[i+1]));
				++i;
			}
		} else
		if (stricmp(argv[i], "-dbstats")==0){
			if ( argc <= i+2 )
				gimmeLog(LOG_FATAL, "No output file or dbnum passed to -dbstats");
			else
			{
				if ( argc <= i+2 )
					gimmeOutputDatabaseStats(argv[i+2], atoi(argv[i+1]), 0);
				else
					gimmeOutputDatabaseStats(argv[i+2], atoi(argv[i+1]), atoi(argv[i+3]));
				i += 2;
			}
		} else
		if (stricmp(argv[i], "-useCache")==0) {
			gimme_state.updateRemoteFileCache = 0;
		} else
		if (strnicmp(argv[i],"--R", 3)==0) {
			printf("Running external tool %s...\n", argv[i]+3);
			system(argv[i]+3);
			printf("Done running external tool.\n");
		} else
		if (stricmp(argv[i], "--test")==0) { // misc testing code
			FWStatType sbuf;
			printf("time adjust: %d\n", gimmeGetTimeAdjust());
			pststat("N:\\revisions\\timestamp\\Jimb.txt", &sbuf);
			printf("N: %d (%d)\n", sbuf.st_mtime, sbuf.st_atime);
			pststat("Y:\\timestamp\\Jimb.txt", &sbuf);
			printf("Y: %d (%d)\n", sbuf.st_mtime, sbuf.st_atime);
			pststat("C:\\Jimb.txt", &sbuf);
			printf("C: %d (%d)\n", sbuf.st_mtime, sbuf.st_atime);
			printf("N AltStat: %d\n", fileLastChangedAltStat("N:\\revisions\\timestamp\\Jimb.txt"));
			printf("Y AltStat: %d\n", fileLastChangedAltStat("Y:\\timestamp\\Jimb.txt"));
			printf("C AltStat: %d\n", fileLastChangedAltStat("C:\\Jimb.txt"));

			printf("C: Samba : %d\n", gimmeIsSambaDrive("C:\\blarg"));
			printf("N: Samba : %d\n", gimmeIsSambaDrive("N:\\blarg"));
			printf("\\\\penguin3\\data\\ Samba : %d\n", gimmeIsSambaDrive("\\\\penguin3\\data\\blarg"));
			printf("\\\\vesta\\users\\ Samba : %d\n", gimmeIsSambaDrive("\\\\vesta\\users\\blarg"));
			printf("\\\\mars\\revisions\\ Samba : %d\n", gimmeIsSambaDrive("\\\\mars\\revisions\\blarg"));
			printf("Y: Samba : %d\n", gimmeIsSambaDrive("Y:\\blarg"));
			printf("Z: Samba : %d\n", gimmeIsSambaDrive("Z:\\blarg"));

			gimme_state.pause=1;
			return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
		} else
		if (stricmp(argv[i], "-noskipbins")==0) { // NOTE: Workaround hack to make oldgimme accept this argument so the builders will shutup. <NPK 2008-10-15>
			if (argc > i+1 && (stricmp(argv[i+1], "0")==0 || stricmp(argv[i+1], "1")==0))
				i += 1;
		} else
		if (argv[i][0]=='-' && (argc!=i+1)) {
			gimmeLog(LOG_FATAL, "Error: invalid command line option : %s", argv[i]);
			gimme_state.pause=true;
			return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
		} else if ( reading_script ) {
			char temp_buffer[512], temp_char;
			int cur, done = 0;

			script_name = makeLongPathName(argv[i], long_path_name);
			script = fopen( script_name, "rt" );
			if ( !script )
			{
				gimmeLog(LOG_FATAL, "Error: Could not open script : %s", script_name);
				return returnCleanupPause(GIMME_ERROR_COMMANDLINE);
			}

			while ( !done )
			{
				temp_buffer[0] = 0;
				while( temp_buffer[0] != '\"' )
				{
					if ( !fread( temp_buffer, sizeof(char), 1, script ) )
					{
						done = 1;
						break;
					}
				}

				if ( !done )
				{
					cur = 0;
					if ( !fread( &temp_char, sizeof(char), 1, script ) )
					{
						done = 1;
						continue;
					}

					while ( temp_char != '\"' )
					{
						temp_buffer[cur++] = temp_char;
						if ( !fread( &temp_char, sizeof(char), 1, script ) )
						{
							done = 1;
							break;
						}
					}
					temp_buffer[cur] = 0;
				}

				if ( !done )
				{
					fname = makeLongPathName( temp_buffer, long_path_name );

					if (getCommentLevel(fname) != CM_DONTASK && put_back==GIMME_CHECKIN) {
						gimme_state.just_queue=1;
					}
					if (NO_ERROR != (tempret = gimmeDoOperation(fname,put_back,gimme_state.quiet))) {
						// error
					} else {
						// no error
					}
					if (getCommentLevel(fname) != CM_DONTASK && put_back==GIMME_CHECKIN) {
						gimme_state.just_queue=0;
						if (tempret==NO_ERROR) {
							//tempret = doQueuedActions();
						} else {
							HANDLE_RET_NOPAUSE(tempret);
						}
					}
				}
			}

			reading_script = 0;

		} else {
			// assume this is a filename!
			fname = makeLongPathName(argv[i], long_path_name);
			if (getCommentLevel(fname) != CM_DONTASK && put_back==GIMME_CHECKIN) {
				gimme_state.just_queue=1;
			}
			if (NO_ERROR != (tempret = gimmeDoOperation(fname,put_back,gimme_state.quiet))) {
				// error
			} else {
				// no error
			}
			if (getCommentLevel(fname) != CM_DONTASK && put_back==GIMME_CHECKIN) {
				gimme_state.just_queue=0;
				if (tempret==NO_ERROR) {
					//ret = doQueuedActions();
				} else {
					
				}
			}
			HANDLE_RET_NOPAUSE(tempret);
		}
	}

	// do all the work now
	doQueuedActions();

	gimmeDirDatabaseCloseAll();

	return returnCleanupPause(ret);
}

GimmeErrorValue gimmeDoCommandWrapper(int argc, char *argv[])
{
	int i;
	bool is_gimme_exe=false;
	GimmeErrorValue ret=GIMME_NO_ERROR;
	if (strEndsWith(argv[0], "gimme.exe") || strEndsWith(argv[0], "gimmeFD.exe")) {
		is_gimme_exe = true;
	}
	if (is_gimme_exe) {
		// Stand-alone, going to exit after calling this
		char *estr = NULL;
		setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'G', 0x00ff50);
		estrPrintf(&estr, "Gimme: ");
		for(i=1;i<argc;i++)
		{
			estrConcatf(&estr, "%s ", argv[i]);
		}
		SetConsoleTitle_UTF8(estr);
		estrDestroy(&estr);

		// Use more memory, since we're in stand-alone mode.
		//patchmeUseMoreMemory(); people are getting OOM crashes, sigh: see COR-15791

		ret = patchmeDoCommandWrapperInternal(argc, argv, &ret) ? ret : gimmeDoCommandWrapperInternal(argc, argv);

		_chdir("C:\\"); // seems to help windows out if we were doing deletions, etc =)
		for (i=0; i<eaiSize(&gimme_state.databases_to_update_hoggs); i++) {
			gimmeUtilCheckForHoggUpdate(gimme_state.databases_to_update_hoggs[i]);
			gimmeUtilCheckForRunEvery(gimme_state.databases_to_update_hoggs[i]);
		}

		gimmeUtilCheckAutoRegister();

		if(gimme_state.doStartFileWatcher){
			startFileWatcher();
		}
	} else {
		// Called from another program
		ret = patchmeDoCommandWrapperInternal(argc, argv, &ret) ? ret : gimmeDoCommandWrapperInternal(argc, argv);
	}
	return ret;
}

GimmeErrorValue gimmeDoCommand(const char *cmdline)
{
	char *buf = strdup(cmdline);
	GimmeErrorValue ret;
#define MAX_GIMME_ARGS 1000
	char *argv[MAX_GIMME_ARGS];
	char *next;
	int argc;
	int i;

	argc = tokenize_line(buf, argv, &next);
	assert( argc < MAX_GIMME_ARGS );
	// Add the executable name to the beginning of the command line.
	// (argv[0] is ignored later)
	for (i=argc; i>0; i--) 
		argv[i] = argv[i-1];
	argc++;
	argv[0] = getExecutableName();
	ret = gimmeDoCommandWrapper(argc, argv);
	SAFE_FREE(buf);
	return ret;
}
