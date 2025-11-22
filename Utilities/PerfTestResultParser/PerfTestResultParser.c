#include "MemoryMonitor.h"
#include "cmdParse.h"
#include "FolderCache.h"
#include "sysUtil.h"
#include "file.h"
#include "fileutil2.h"
#include "earray.h"
#include "utils.h"

//this program should look in the current directory for all files named timeStamp_*.txt, glom them together
//and the write out summary_timeStamp.txt

char timeStamp[128] = "";
AUTO_CMD_STRING(timeStamp, timeStamp);

void trimWhitespace(char *buf)
{
	int len;
	while (*buf && strchr("\r\n\t ", *buf))
		strcpy_unsafe(buf, buf+1);
	len = (int)strlen(buf);
	while (len && strchr("\r\n\t ", buf[len-1]))
		len--;
	buf[len]= '\0';
}

int wmain(int argc, WCHAR** argv_wide)
{

	FILE *pOutFile;
	char outFileName[CRYPTIC_MAX_PATH];
	char **filenames;
	int i;
	char expr[1024];
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();

	cmdParseCommandLine(argc, argv);

	FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);
	preloadDLLs(0);
	fileAllPathsAbsolute(true);

	assert(timeStamp[0]);

	sprintf(outFileName, "summary_%s.txt", timeStamp);
	pOutFile = fopen(outFileName, "wt");
	assert(pOutFile);

	fprintf(pOutFile, "\n");

	filenames = fileScanDirNoSubdirRecurse(".");
	sprintf(expr, "./%s_*.csv", timeStamp);
	for (i=0; i<eaSize(&filenames); i++) {
		if (simpleMatch(expr, filenames[i]) && !strEndsWith(filenames[i], "history.csv")) {
			int data_len;
			char *data = fileAlloc(filenames[i], &data_len);
			char buf[1024];
			char *context=NULL;
			char *line;
			char demo_name[1024]="";
			float mspf=-1, fps=-1, stall_time=-1;
			int mspf_98=-1, stall_count=-1;
			assert(data);
			//printf("%s\n", getLastMatch());

			line = strtok_s(data, "\r\n", &context);
			while (line) {
				char *args[100];
				int j;
				int numargs = tokenize_line_quoted_delim(line, args, ARRAY_SIZE(args), &line, ",", ",");
				for (j=0; j<numargs; j++)
					trimWhitespace(args[j]);
				if (!demo_name[0] && numargs >= 2) {
					strcpy(demo_name, args[1]);
				}
				if (numargs >= 4) {
#define CASE(s) (stricmp(args[2], (s))==0)
					if (CASE("fps")) {
						fps = atof(args[5]);
					} else if (CASE("ms")) {
						mspf = atof(args[5]);
					} else if (CASE("stalls")) {
						stall_count = atoi(args[3]);
						stall_time = atof(args[4]);
					} else if (CASE("98th percentile ms/f")) {
						mspf_98 = atoi(args[3]);
					}
				}
				line = strtok_s(NULL, "\r\n", &context);
			}
			sprintf(buf, "%-22s Avg: %4.1f ms/f (%7.3f fps), 98th:%3d ms/f (%4.1f fps), Stalls:%4d /%5.2fs",
				demo_name, mspf, fps, mspf_98, (float)(1000.f/mspf_98), stall_count, stall_time);
			printf("%s\n", buf);
			fprintf(pOutFile, "%s\n", buf);
			//MS_XMA_INTERIOR   Avg: 18.8 ms/f (53.073 fps), 98th: 21 ms/f (47.6 fps), Stalls: 54 / 4.18s
			fileFree(data);
		}
	}

	fileScanDirFreeNames(filenames);

	fclose(pOutFile);




	EXCEPTION_HANDLER_END



	return 0;
}











