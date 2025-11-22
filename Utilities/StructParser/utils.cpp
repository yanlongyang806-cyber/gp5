#include "stdio.h"
#include "tokenizer.h"
#include "winbase.h"
#include "psapi.h"
#include "sys/stat.h"


int printf_wrapped(const char *fmt, ... ) 
{ 
     va_list va; 
     char buf[2048]; 
 
     va_start(va, fmt); 
     vsprintf_s(buf, 2048, fmt, va); 
     va_end(va); 
 
     OutputDebugString(buf); 
#undef printf 
     return printf("%s", buf); 
} 



int timeApproxDifInMinutes(SYSTEMTIME *pTime1, SYSTEMTIME *pTime2)
{
	return (pTime2->wDay - pTime1->wDay) * 24 * 60 + (pTime2->wHour - pTime1->wHour) * 60 + (pTime2->wMinute - pTime1->wMinute);
}


FILE *fopen_nofail(const char *pFileName, const char *pModes)
{
	FILE *pRetVal = fopen(pFileName, pModes);

	STATICASSERTF(pRetVal != NULL, "Couldn't open file %s(%s)", pFileName, pModes);

	return pRetVal;
}

void BreakIfInDebugger()
{
#ifdef _DEBUG
	if (IsDebuggerPresent())
	{
		DebugBreak();
	}
#endif
}




BOOL ProcessNameMatch( DWORD processID , char * targetName)
{
	char szProcessName[512] = "unknown";

	// Get a handle to the process.

	HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION |
		PROCESS_VM_READ,
		FALSE, processID );

	// Get the process name.

	if (NULL != hProcess )
	{
		HMODULE hMod;
		DWORD cbNeeded;

		if ( EnumProcessModules( hProcess, &hMod, sizeof(hMod), 
			&cbNeeded) )
		{
			GetModuleBaseName( hProcess, hMod, szProcessName, 
				sizeof(szProcessName) );
		}
		else {
			CloseHandle( hProcess );
			return FALSE;
		}
	}
	else return FALSE;

	// Print the process name and identifier.
	CloseHandle( hProcess );

	if (_stricmp(szProcessName, targetName)==0)
		return TRUE;
	else
		return FALSE;
}

int ProcessCount(char * procName)
{
	// Get the list of process identifiers.
	DWORD aProcesses[1024], cbNeeded, cProcesses;
	unsigned int i;
	int count = 0;

	if ( !EnumProcesses( aProcesses, sizeof(aProcesses), &cbNeeded ) )
		return 0;

	// Calculate how many process identifiers were returned.

	cProcesses = cbNeeded / sizeof(DWORD);

	// Print the name and process identifier for each process.

	for ( i = 0; i < cProcesses; i++ )
	{
		if(ProcessNameMatch( aProcesses[i] , procName))
			count++;
	}
	return count;
}



int dirExists(const char *dirname)
{
	struct _stat32 status;

	if(!_stat32(dirname, &status)){
		if(status.st_mode & _S_IFDIR)
			return 1;
	}
	
	return 0;
}


char *fileAlloc(const char *pFileName, int *piOutSize)
{
	FILE *pFile = fopen(pFileName, "rb");
	char *pBuf;

	if (!pFile)
	{
		if (piOutSize)
		{
			*piOutSize = 0;
		}

		return NULL;
	}
	

	fseek(pFile, 0, SEEK_END);

	int iFileSize = ftell(pFile);
	if (piOutSize)
	{
		*piOutSize = iFileSize;
	}

	fseek(pFile, 0, SEEK_SET);

	pBuf = (char*)malloc(iFileSize + 1);
	pBuf[iFileSize] = 0;
	fread(pBuf, iFileSize, 1, pFile);
	fclose(pFile);

	return pBuf;
}

bool IsContinuousBuilder(void)
{
	static int CBRunning = -1;

	if (CBRunning == -1)
	{
		CBRunning = ProcessCount("continuousbuilder.exe");
	}

	return !!CBRunning;
}

extern int gDontQuerySVN;

int GetSVNVersionAndBranch(char *pFileName, char *pOutBranch, bool bTrimToDir)
{

	char tempFileName[MAX_PATH];
	char systemString[1024];
	char fileNameCopy[MAX_PATH];

	if (gDontQuerySVN)
	{
		strcpy(pOutBranch, "(Unknown)");
		return 1;
	}

	GetTempFileName(".", "SPR", 0, tempFileName);

	strcpy(fileNameCopy, pFileName);

	if (bTrimToDir)
	{
		char *pLastSlash = strrchr(fileNameCopy, '\\');
		if (!pLastSlash)
		{
			pLastSlash = strrchr(fileNameCopy, '/');
		}

		*pLastSlash = 0;
		int iLen = strlen(fileNameCopy);
		while (fileNameCopy[iLen - 1] == '\\' || fileNameCopy[iLen - 1] == '/')
		{
			fileNameCopy[iLen - 1] = 0;
			iLen--;
		}
	}

	sprintf(systemString, "C:\\Cryptic\\tools\\bin\\svn.exe info \"%s\" > \"%s\"", fileNameCopy, tempFileName);
	if (0!=system(systemString))
	{
		// Hope it's in the path
		sprintf(systemString, "svn info \"%s\" > \"%s\"", pFileName, tempFileName);
		system(systemString);
	}

	Tokenizer tokenizer;

		
	bool bResult = tokenizer.LoadFromFile(tempFileName);

	sprintf(systemString, "del \"%s\"", tempFileName);
	system(systemString);


	if (!bResult)
	{
		return 0;
	}


	Token token;
	enumTokenType eType;

	do
	{
		bool bBeginningOfLine = tokenizer.IsBeginningOfLine();

		eType = tokenizer.GetNextToken(&token);

		if (eType == TOKEN_NONE)
		{
			return 0;
		}

		if (bBeginningOfLine && eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "URL") == 0)
		{
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COLON, "Expected : after Revision");
			tokenizer.SetExtraCharsAllowedInIdentifiers(":/.-;");
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 512, "Expected identifier after URL:");
			tokenizer.SetExtraCharsAllowedInIdentifiers("");


			if (StringEndsWith(token.sVal, ".vcxproj"))
			{
				token.sVal[token.iVal - 8] = 0;
			}
			else if (StringEndsWith(token.sVal, ".vcproj"))
			{
				token.sVal[token.iVal - 7] = 0;
			}

			if (strlen(token.sVal) > 127)
			{
				tokenizer.AssertFailedf("SVN branch %s is too long, exceeds 127 characters");
			}
			strcpy(pOutBranch, token.sVal);		}



		if (bBeginningOfLine && eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "Revision") == 0)
		{
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COLON, "Expected : after Revision");
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Expected int after Revision:");

			return token.iVal;
		}
	}
	while (1);

	return 0;	
}
