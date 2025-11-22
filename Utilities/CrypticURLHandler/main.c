#include "textparser.h"
#include "RegistryReader.h"
#include "HttpLib.h"
#include "FolderCache.h"
#include "file.h"
#include "error.h"
#include "sysutil.h"
#include "wininclude.h"
#include "pcre.h"
#include "UTF8.h"

#include "AutoGen/main_c_ast.h"

#define URL_SCHEME "cryptic"
#define GIMME_CONFIG "C:/Night/tools/bin/urlhandler.cfg"
#define NETWORK_CONFIG "N:/urlhandler.cfg"
#define LOCAL_CONFIG "C:/CrypticSettings/urlhandler.cfg"


AUTO_STRUCT;
typedef struct HandlerProgram {
	char *name; AST(NAME("Name"), STRUCTPARAM)
	char *default_args; AST(NAME("DefaultArgs"), DEFAULT(""))
	char **executable; AST(NAME("Executable"))
	char *arg_format; AST(NAME("ArgFormat"), DEFAULT("%s"))
} HandlerProgram;

AUTO_STRUCT;
typedef struct HandlerConfig {
	HandlerProgram **programs; AST(NAME("Program"))
} HandlerConfig;

#define setRegKey(key, name, value)     \
	reader = createRegReader();         \
    initRegReader(reader, key);         \
    rrWriteString(reader, name, value); \
    destroyRegReader(reader)

static void initURLHandler(void)
{
	char prog[CRYPTIC_MAX_PATH];
	RegReader reader;
	char key[CRYPTIC_MAX_PATH];
	char value[CRYPTIC_MAX_PATH];

	strcpy(prog, getExecutableName());
	backSlashes(prog);

	sprintf(key, "HKEY_CLASSES_ROOT\\%s", URL_SCHEME);
	setRegKey(key, "", "URL:Cryptic Protocol Handler");
	setRegKey(key, "URL Protocol", "");

	sprintf(key, "HKEY_CLASSES_ROOT\\%s\\DefaultIcon", URL_SCHEME);
	setRegKey(key, "", prog);

	sprintf(key, "HKEY_CLASSES_ROOT\\%s\\shell\\open\\command", URL_SCHEME);
	sprintf(value, "%s %%1", prog);
	setRegKey(key, "", value);
}

int APIENTRY wWinMain(HINSTANCE hInstance,
					 HINSTANCE hPrevInstance,
					 WCHAR*    pWideCmdLine,
					 int       nCmdShow)
{
	char *config_file = NULL;
	HandlerConfig config = {0};
	pcre *re;
	int match, ovector[30];
	char *re_str;
	int re_erroroff;


	EXCEPTION_HANDLER_BEGIN
	char *lpCmdLine = UTF16_to_UTF8_CommandLine(pWideCmdLine);	

	DO_AUTO_RUNS
	FolderCacheChooseMode();

	if(strlen(lpCmdLine) == 0)
	{
		initURLHandler();
		return 0;
	}

	if(fileExists(GIMME_CONFIG))
	{
		config_file = GIMME_CONFIG;
	}
	else if(fileExists(NETWORK_CONFIG))
	{
		config_file = NETWORK_CONFIG;
		makeDirectoriesForFile(LOCAL_CONFIG);
		fileCopy(NETWORK_CONFIG, LOCAL_CONFIG);
	}
	else if(fileExists(LOCAL_CONFIG))
	{
		config_file = LOCAL_CONFIG;
	}
	else
	{
		FatalErrorf("Cannot locate config file");
	}

	if(!ParserReadTextFile(config_file, parse_HandlerConfig, &config, 0))
		FatalErrorf("Could not parse config file %s", config_file);

	re = pcre_compile("^" URL_SCHEME "://(\\w+)(?:/(.+))?", 0, &re_str, &re_erroroff, NULL);
	assertmsg(re, re_str);
	match = pcre_exec(re, NULL, lpCmdLine, (int)strlen(lpCmdLine), 0, 0, ovector, 30);
	assertmsgf(match > 0, "Got an invalid URL string: %s    (%d)", lpCmdLine, match);
	pcre_get_substring(lpCmdLine, ovector, match, 1, &re_str);
	FOR_EACH_IN_EARRAY(config.programs, HandlerProgram, prog)
		if(!strcmpi(prog->name, re_str))
		{
			FOR_EACH_IN_EARRAY(prog->executable, char, exe)
				if(fileExists(exe))
				{
					char args[1024], cmd[4096];
					
					pcre_free(re_str);
					if(match > 2)
					{
						char *argstr;
						int argstr_len;
						pcre_get_substring(lpCmdLine, ovector, match, 2, &argstr);
						argstr_len = (int)strlen(argstr);
						re_str = malloc(argstr_len+1);
						urlUnescape(argstr, re_str, argstr_len+1);
						pcre_free(argstr);
						sprintf(args, FORMAT_OK(prog->arg_format), re_str);
						free(re_str);
					}
					else
						strcpy(args, "");
					sprintf(cmd, "%s %s %s", exe, prog->default_args?prog->default_args:"", args);
					system_detach(cmd, false, false);
					return 0;
				}
			FOR_EACH_END
			MessageBox_UTF8(NULL, STACK_SPRINTF("Could not find any executable for %s", prog->name), "Can't find executable", MB_ICONEXCLAMATION);
		}
	FOR_EACH_END

	
	EXCEPTION_HANDLER_END 

	return 1;
}

#include "AutoGen/main_c_ast.c"