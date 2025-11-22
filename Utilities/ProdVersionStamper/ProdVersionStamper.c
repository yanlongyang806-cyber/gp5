#include "memoryMonitor.h"
#include "cmdParse.h"
#include "file.h"
#include "folderCache.h"
#include "sysUtil.h"
#include "utilitiesLib.h"
#include "earray.h"
#include "gimmeDLLWrapper.h"

char exeName[CRYPTIC_MAX_PATH] = "";
AUTO_CMD_STRING(exeName, exeName);

char newVersionName[300] = "";
AUTO_CMD_SENTENCE(newVersionName, newVersionName);

//the number of places to expect to find the magic numbers and do the stamping. Certain executables
//include other executables, so have multiple copies of the magic version struct
int iNumToStamp = 1;
AUTO_CMD_INT(iNumToStamp, NumToStamp);

extern U32 *GetProdVersionMagicNumbers(void);


void DoVersionStamping(char *pExeName, char *pVersionString)
{
	int iSize;
	U32 *pMagicNumbers = GetProdVersionMagicNumbers();
	char *pBuf = fileAlloc(pExeName, &iSize);
	
	U32 *piFoundOffsets = NULL;

	U32 *pBufInts;
	int i;
	char *pTargetString;
	FILE *pOutFile;

	if (strlen(pVersionString) > 255)
	{
		printf("version string too long\n");
		exit(-1);
	}

	if (!pBuf)
	{
		printf("Couldn't load %s\n", pExeName);
		exit(-1);
	}

	pBufInts = (U32*)pBuf;
	for (i=0; i < (iSize/4) - 3; i++)
	{
		if ((pBufInts[i] == pMagicNumbers[0])
		&& (pBufInts[i+1] == pMagicNumbers[1])
		&& (pBufInts[i+2] == pMagicNumbers[2])
		&& (pBufInts[i+3] == pMagicNumbers[3]))
		{
			ea32Push(&piFoundOffsets, i * 4);
		}
	}

	if (ea32Size(&piFoundOffsets) != iNumToStamp)
	{
		printf("Expected %d magic numbers, found %d instead: ", 
			iNumToStamp, ea32Size(&piFoundOffsets));
		for (i=0; i < ea32Size(&piFoundOffsets); i++)
		{
			printf(" %x", piFoundOffsets[i]);
		}
		printf("\n");
		exit(-1);
	}

	for (i=0; i < ea32Size(&piFoundOffsets); i++)
	{
		pTargetString = pBuf + piFoundOffsets[i] + 16;

		memset(pTargetString, 0, 256);
		memcpy(pTargetString, pVersionString, strlen(pVersionString));
	}

	pOutFile = fopen(pExeName, "wb");
	if (!pOutFile)
	{
		printf("Couldn't open %s for writing\n", pExeName);
		exit(-1);
	}

	fwrite(pBuf, iSize, 1, pOutFile);
	fclose(pOutFile);
}

int main(int argc,char **argv)
{
	int i;
	bool bNeedToConfigure = false;
	char *pErrorString = NULL;

	EXCEPTION_HANDLER_BEGIN
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	FolderCacheChooseMode();


//	preloadDLLs(0);
	gimmeDLLDisable(1);


//	utilitiesLibStartup();


	cmdParseCommandLine(argc, argv);



	srand((unsigned int)time(NULL));

	fileAllPathsAbsolute(true);

	if (!exeName[0] || !newVersionName[0])
	{
		printf("-exename or -newVersionName not set");
		exit(-1);
	}

	DoVersionStamping(exeName, newVersionName);

	
	EXCEPTION_HANDLER_END

}

