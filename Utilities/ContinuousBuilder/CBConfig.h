#pragma once

/*
The CBConfig struct holds all the configuration and setup info for a continuous builder. It replaces the old CBGlobalState and
CBDynamicState stuff. It depends on two things, the CB Product and CB Type, both of which are set by CBStartup.

It is loaded from a number of places sequentially, so that later loads will override earlier loads (or add to 
earrays) (TYPENAME is the SHORT typename, ie "PROD_LOOPING").

There is also type inheritance. For instance, PROD_BASELINE inherits from PROD.

This means that every line which includes TYPENAME will actually get duplicated, so for PROD_BASELINE,
line 3 will turn into two lines:

3a. c:\core\tools\programmers\ContinuousBuilder\configs\CBConfig_PROD.txt
3b. c:\core\tools\programmers\ContinuousBuilder\configs\CBConfig_PROD_BASELINE.txt

Note that PROD is not actually a build type at all, it's just something that all the various actual
production types can inherit from

1. c:\core\tools\ContinuousBuilder\configs\CBConfig.txt
2. (if product != core) c:\productname\tools\ContinuousBuilder\configs\CBConfig.txt
3. c:\core\tools\ContinuousBuilder\configs\CBConfig_TYPENAME.txt
4. (if product != core) c:\productname\tools\ContinuousBuilder\configs\CBConfig_TYPENAME.txt
5. n:\continuousBuilder\configs\CBConfig.txt
6. n:\continuousBuilder\configs\CBConfig_TYPENAME.txt
7. c:\continuousBuilder\CBConfig.txt
8. c:\continuousBuilder\CBConfig_TYPENAME.txt
9. c:\continuousBuilder\configs\productname\CBConfig.txt
10. c:\continuousBuilder\configs\productname\CBConfig_TYPENAME.txt
*/

typedef struct StashTableImp*			StashTable;

AUTO_STRUCT;
typedef struct FileToLink
{
	char *pLinkName;
	char *pFileName;
} FileToLink;

AUTO_STRUCT;
typedef struct StartingVariable
{
	char *pVarName; AST(STRUCTPARAM)
	char *pVarValue; AST(STRUCTPARAM)
} StartingVariable;


//if pDevModeDefaultValue is set, that means this variable has
//a different default in Dev mode. For instance, DONT_CHECKIN
//defaults to 1 in dev mode, 0 otherwise
AUTO_STRUCT;
typedef struct OverrideableVariable
{
	char *pVarName; AST(STRUCTPARAM)
	char *pNormalDefaultValue; AST(STRUCTPARAM)
	char *pDevModeDefaultValue;
	char *pDescription; AST(STRUCTPARAM)
	char *pCurVal; AST(ESTRING)

	//if set, must be the name of another override in this same list. This overridable will only show up
	//if the parent is set to a non-empty, non-"0" value.
	char *pParentOverride; AST(STRUCTPARAM)

	//if true, the CB interface will only allow sorted lists of comma-separated integers to be entered here
	bool bSortedIntList;

	//if true, will comma-separate the items typed into the zoomed-in text editor back to the normal 
	//text editor, and vice versa
	bool bCommaSeparatedZoomInList;
		
} OverrideableVariable;


AUTO_STRUCT;
typedef struct RegExPrefixStripper
{
	char *pMatchRegEx; AST(STRUCTPARAM)
	char *pPrefixEnd; AST(STRUCTPARAM)
} RegExPrefixStripper;

//info needed to parse compile errors out of the redirected output of a command line compile attempt
AUTO_STRUCT;
typedef struct CompileErrorConfig
{
	char *pSignatureString; //if you find this string, then use this config. (if no such string is found, use the last config)

	char **ppErrorStrings; //if you find this string in the compile output, that line is reporting an error

	char **ppErrorStringCancellers; //if you find this string in a string already thought to be an error,
							//then ignore this error instead

	char **ppErrorStringCancellerRegExes; //same as above, but uses RegEx

	RegExPrefixStripper **ppRegExPrefixStrippers; //if it matches this regex, then strip everything out of the error up to
		//and including the PrefixEnd

	char **ppFakeCompilerErrors; //if the compile fails and you find this string, it's a "fake" failure
								//(ie, "build process failed to initialize")

	char **ppRetryCompilerErrors; //if you hit one of these, then just try to compile again instantly,
								 //it will probably succeed (up to a small # of retries). For instance,
								 //"LINK : fatal error LNK1000: unknown error"

	
	char *pBeginningOfConfigString; // these two lines are used to parse configuration changes.
	char *pEndOfConfigString;
} CompileErrorConfig;



//config stuff which is specific to build type Continuous Dev Build
AUTO_STRUCT;
typedef struct CBContinuousConfig
{
	char *pCompileScript; //script to run for normal compiles
	char *pFullCompileScript; //script to run for full compiles


	char *pTestScript; //main test script

	char *pNonFatalTestScript; //another script to run, and report errors from, but failures in it are non-fatal,
		//ie, errors instead of crashes/asserts/timeouts

	char *pCheckinScript; //main checkin script for good builds
	char *pBadCheckinScript; //checkin script for bad builds (updates symbols only so that dump files will work)

	char *pMagicSVNCheckinEmailString; //strings that can be put into an SVN checkin which will cause the CB to send
		//email to these people when that checkin is built and checked in. Ie, "CBEmail:"

	bool bWriteMagicProdBuildFile;
		//if true, then this CB will write the magic file to N that indicates a good SVN and gimme rev for the production builder
		//to use

	char **ppPeopleWhoAlwaysWantCheckinNotification; //list of names of people who should always get CBEmailed their own checkins


} CBContinuousConfig;


AUTO_STRUCT;
typedef struct CBConfig
{
	char **ppSVNFolders; AST(ESTRING NAME(SVNFolder))//what SVN folders you need to get in orer to
		//build the project. (PRODUCTNAME will be replaced)
	
	char **ppGimmeFolders; AST(ESTRING NAME(GimmeFolder))//what Gimme folders you need to get in order to 
		//run the product. (PRODUCTNAME will be replaced)

	char **ppScriptDirectories; AST(ESTRING NAME(ScriptDirectory))//directories in which build scripts might be found

	bool bDontGetNewScripts; //usually we always get the newest versions of the script directories
		//before doing any loading. This turns that off.

	CBContinuousConfig continuousConfig;

	char **ppDefaultEmailRecipient; //people who get all normal status updates

	char **ppAdministrator; //people who get emailed when things go wrong

	char *pGlobalDumpLocation; AST(DEF("n:\\ContinuousBuilder\\Dumps"))

	char *pScript; //main script the CB will run

	bool bLooping; //if true, run the script over and over again. Otherwise, run it once and stop.

	//a list of filenames to provide links to on the front web page. This is how the
	//production build history shows up on production builders
	FileToLink **ppFilesToLink;

	StartingVariable **ppStartingVariable; //variables which always start out set
		//in build scripting

	OverrideableVariable **ppOverrideableVariables; AST(NAME(Overrideable))//variables which have a default value
		//but can be overridden via UI
	
	OverrideableVariable **ppDevOnlyOverrideableVariables; AST(NAME(DevOnlyOverrideable))
		//same as previous, but only for dev mode

	char **ppFakeCompileErrorMessages; //strings such as "could not connect to xbox"
		//which indicate that a failed compile didn't really mean anything bad happened, so no emails
		//should be sent out (except to the continuous builder's administrators)

	/*should be generally set on machines of people who are testing CB.exe and running
	  local copies of things. Does various things:
	  (1) doesn't create desktop shortcuts
	  (2) all emails and jabbers will be sent only to the current user's username
	  (3) no checkins 
	  (4) sets $CB_DEV$ to 1 in all build scripts (which is used to prevent pushing to patchserver in build scripts)
	  (5) doesn't do status reporting
	*/
	bool bDev;
	
	//report to CBMonitor even if bDev is true (presumably for testing of CBMonitor stuff)
	bool bForceReportToCBMonitor;


	bool bQuitOnScriptCompletion; //if true, and if this is a non-looping builder, then quit when the script is done.

	char **ppFieldNamesNotToInherit; //set xpath-style names of fields which should be cleared BEFORE loading
									 //the rest of this file

	char *pWhereToSendStatusReports; //by default, non-dev CBs always do status reporting

	CompileErrorConfig **ppCompileConfigs;

	char **ppVariablesToReportToCBMonitor; //whenever a variable on this list is set, report that to the CB monitor

	char **ppVariablesToReportToCBMonitorOnHeartbeat; //super-important variables to report every minute 
		//everything in this list is automatically treated as being in ppVariablesToReportToCBMonitor

	//stashtable versions of the above for quick lookup
	StashTable sVariablesToReportToCBMonitor; NO_AST

	//set to true when the config is finished loading
	bool bLoaded; NO_AST
} CBConfig;
	

void CB_LoadConfig(bool bFirstTime);

void CB_SetScriptingVariablesFromConfig(void);

//asserts that the named variable exists, then returns 1 or 0
//(returns 0 on "0" or all-whitespace)
bool CheckConfigVar(char *pVarName);

//like above, but returns 0 on nonexistant
bool CheckConfigVarExistsAndTrue(char *pVarName);

//returns the named variable, or NULL
char *GetConfigVar(char *pVarName);

//repeatedly recursively replaces all variables (with dollarsigns) in the input string
//with their value
void ReplaceConfigVarsInString(char **ppString);

extern CBConfig gConfig;









