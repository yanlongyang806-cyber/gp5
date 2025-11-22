#include "resourceinfo_utils.h"
#include "Windows.h"

// No, I really _don't_ want to use Cryptic's fopen.
#undef fopen

char *fileAlloc(char *fname, int *lenp)
{
	size_t len, n;
	char *data;
	FILE *file = fopen(fname, "rb");
	if(!file)
		return NULL;
	n = fseek(file, 0, SEEK_END);
	len = ftell(file);
	n = fseek(file, 0, SEEK_SET);
	data = malloc(len);
	if(!data)
		return NULL;
	n = fread(data, 1, len, file);
	if(n != len)
	{
		free(data);
		return NULL;
	}
	if(lenp)
		*lenp = (int)len;
	return data;
}

static char *g_resource_types[] = {
	"BITMAP", RT_BITMAP,
	"ICON", RT_ICON,
	"DIALOG", RT_DIALOG,
	"CURSOR", RT_CURSOR
};

const char *parse_resource_id(const char *str)
{
	char *end;
	int n;

	// Check for #123 format
	if(str && str[0] == '#')
	{
		n = strtol(str+1, &end, 10);
		if(end == str+1)
		{
			fprintf(stderr, "Cannot parse %s as a number\n", str+1);
			return NULL;
		}
		return MAKEINTRESOURCE(n);
	}

	// Check for 123 format
	n = strtol(str, &end, 10);
	if(end == str + strlen(str))
		return MAKEINTRESOURCE(n);

	// Remove RT_ prefix if present
	if(strnicmp(str, "RT_", 3)==0)
		str += 3;

	for(n=0; n<ARRAY_SIZE_CHECKED(g_resource_types); n+=2)
	{
		if(stricmp(str, g_resource_types[n])==0)
			return g_resource_types[n+1];
	}

	return str;
}

char *create_resource_from_type_and_file(const char *type, char *filedata, int filedatalen, int *resdatalen)
{
	if(type == (const char *)RT_BITMAP && filedatalen > 14)
	{
		// Bitmap resources (RT_BITMAP=2) are in the same format as .bmp files, excluding the BITMAPFILEHEADER.
		*resdatalen = filedatalen - 14;
		return (char*)((INT_PTR)filedata + (INT_PTR)14);
	}

	// Normal case: Just use the raw data directly
	*resdatalen = filedatalen;
	return filedata;
}
