#include "Windows.h"
#include "sysutil.h"
#include "cmdparse.h"
#include "gimmeDLLWrapper.h"

#include "resourceinfo_utils.h"

char *g_separator = "\n";

AUTO_COMMAND ACMD_CMDLINE;
void separator(char *sep)
{
	g_separator = strdup(sep);
}

bool g_hex = false;
AUTO_CMD_INT(g_hex, hex) ACMD_CMDLINE;

void* get_versioninfo(char *file, char* query, DWORD *out_size)
{
	DWORD size, handle;
	void *data, *buffer, *return_buffer;

	size = GetFileVersionInfoSize(file, &handle);
	if(size == 0)
	{
		fprintf(stderr, "File %s not found\n", file);
		return NULL;
	}
	data = malloc(size);
	assert(GetFileVersionInfo(file, handle, size, data));
	assert(VerQueryValue(data, query, &buffer, &size));
	assert(size);
	return_buffer = malloc(size);
	memcpy(return_buffer, buffer, size);
	if(out_size) *out_size = size;
	free(data);
	return return_buffer;
}

AUTO_COMMAND ACMD_CMDLINE;
void fileversion(char *file)
{
	U32 a, b, c, d;
	VS_FIXEDFILEINFO *fi = get_versioninfo(file, "\\", NULL);
	if(!fi)
		return;
	a = HIWORD(fi->dwFileVersionMS);
	b = LOWORD(fi->dwFileVersionMS);
	c = HIWORD(fi->dwFileVersionLS);
	d = LOWORD(fi->dwFileVersionLS);
	printf("%u.%u.%u.%u%s", a, b, c, d, g_separator);
}

AUTO_COMMAND ACMD_CMDLINE;
void fileinfolanguages(char *file)
{
	DWORD size, i;
	struct LANGANDCODEPAGE {
		WORD wLanguage;
		WORD wCodePage;
	} *trans;
	
	trans = get_versioninfo(file, "\\VarFileInfo\\Translation", &size);
	if(!trans)
		return;
	for(i = 0; i < (size / sizeof(struct LANGANDCODEPAGE)); ++i)
	{
		printf("%04x%04x%s", trans[i].wLanguage, trans[i].wCodePage, g_separator);
	}
}

AUTO_COMMAND ACMD_CMDLINE;
void fileinfostring(char *str, char *file)
{
	char *val = get_versioninfo(file, STACK_SPRINTF("\\StringFileInfo\\040904b0\\%s", str), NULL);
	printf("%s%s", val, g_separator);
}

BOOL CALLBACK resourcetypes_cb(HMODULE hModule, LPTSTR lpszType, LONG_PTR lParam)
{
	if(IS_INTRESOURCE(lpszType))
		printf("#%d\n", (int)(intptr_t)lpszType);
	else
		printf("%s\n", lpszType);
	return TRUE;
}

AUTO_COMMAND ACMD_CMDLINE;
void resourcetypes(char *file)
{
	HANDLE h = LoadLibrary(file);
	if(!h)
	{
		fprintf(stderr, "File %s not found: %s\n", file, lastWinErr());
		return;
	}
	EnumResourceTypes(h, resourcetypes_cb, (LONG_PTR)NULL);
	FreeLibrary(h);
}

BOOL CALLBACK resourcenames_cb(HMODULE hModule, LPCTSTR lpszType, LPTSTR lpszName, LONG_PTR lParam)
{
	if(IS_INTRESOURCE(lpszName))
		printf("#%d\n", (int)(intptr_t)lpszName);
	else
		printf("%s\n", lpszName);
	return TRUE;
}

AUTO_COMMAND ACMD_CMDLINE;
void resourcenames(const char *type, char *file)
{
	HANDLE h = LoadLibrary(file);
	if(!h)
	{
		fprintf(stderr, "File %s not found: %s\n", file, lastWinErr());
		return;
	}
	type = parse_resource_id(type);
	EnumResourceNames(h, type, resourcenames_cb, (LONG_PTR)NULL);
	FreeLibrary(h);
}

BOOL CALLBACK resourcelanguages_cb(HANDLE hModule, LPCTSTR lpszType, LPCTSTR lpszName, WORD wIDLanguage, LONG_PTR lParam)
{
	char *fmt = g_hex ? "0x%04x - (0x%04x, 0x%04x)\n" : "%u - (%u, %u)\n";
	printf(FORMAT_OK(fmt), wIDLanguage, PRIMARYLANGID(wIDLanguage), SUBLANGID(wIDLanguage));
	return TRUE;
}

AUTO_COMMAND ACMD_CMDLINE;
void resourcelanguages(const char *type, const char *name, char *file)
{
	HANDLE h = LoadLibrary(file);
	if(!h)
	{
		fprintf(stderr, "File %s not found: %s\n", file, lastWinErr());
		return;
	}
	type = parse_resource_id(type);
	name = parse_resource_id(name);
	EnumResourceLanguages(h, type, name, resourcelanguages_cb, (LONG_PTR)NULL);
	FreeLibrary(h);
}

AUTO_COMMAND ACMD_CMDLINE;
void resource(const char *type, const char *name, int language, char *file)
{
	HANDLE h = LoadLibrary(file), resh;
	HRSRC res;
	unsigned char *data;
	size_t len, n;
	if(!h)
	{
		fprintf(stderr, "File %s not found: %s\n", file, lastWinErr());
		return;
	}
	type = parse_resource_id(type);
	name = parse_resource_id(name);
	res = FindResourceEx(h, type, name, language);
	if(!res)
	{
		fprintf(stderr, "Resource not found: %s\n", lastWinErr());
		FreeLibrary(h);
		return;
	}
	resh = LoadResource(h, res);
	data = LockResource(resh);
	len = SizeofResource(h, res);
	if(g_hex)
	{
		size_t linen, lineend;
		for(n=0; n<len; n += 16)
		{
			lineend = MIN(n+16, len);
			printf("0x%08X ", n);
			for(linen=n; linen<lineend; linen++)
			{
				printf("%02X ", data[linen]);
			}
			for(linen=n; linen<lineend; linen++)
			{
				char c = data[linen];
				if(c < ' ')
					printf(".");
				else
					printf("%c", c);
			}
			printf("\n");
		}
	}
	else
	{
		for(n=0; n<len; n++)
			printf("%c", data[n]);
		printf("\n");
	}
	FreeLibrary(h);
}

AUTO_COMMAND ACMD_CMDLINE;
void updateresource(char *file, const char *type, const char *name, int language, char *newdata)
{
	char *filedata;
	int filedatalen;
	char *resdata;
	int resdatalen;
	BOOL ret;
	HANDLE h = BeginUpdateResource(file, FALSE);
	
	// Decode the type and name
	type = parse_resource_id(type);
	name = parse_resource_id(name);

	// Open the target executable
	if(!h)
	{
		fprintf(stderr, "File %s not found: %s\n", file, lastWinErr());
		return;
	}

	// Load the new data. "DELETE" means to clear the resource.
	if(stricmp(newdata, "DELETE")==0)
	{
		filedata = NULL;
		filedatalen = 0;
		resdata = NULL;
		resdatalen = 0;
	}
	else
	{
		filedata = fileAlloc(newdata, &filedatalen);
		if(!filedata)
		{
			fprintf(stderr, "File %s not found: %s\n", newdata, lastWinErr());
			return;
		}
		resdata = create_resource_from_type_and_file(type, filedata, filedatalen, &resdatalen);
	}

	ret = UpdateResource(h, type, name, language, resdata, resdatalen);
	if(!ret)
	{
		fprintf(stderr, "Unable to update resource: %s\n", lastWinErr());
		return;
	}
	ret = EndUpdateResource(h, FALSE);
	if(!ret)
	{
		fprintf(stderr, "Unable to write updated resources: %s\n", lastWinErr());
		return;
	}
}

extern bool gbPrintCommandLine;

int main(int argc, char **argv)
{
	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS
	
	gbPrintCommandLine = false;
	gimmeDLLDisable(true);
	cmdParseCommandLine(argc, argv);
	
	EXCEPTION_HANDLER_END

	return 0;
}