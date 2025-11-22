#include "wininclude.h"
#include "file.h"
#include "utils.h"
#include "StashTable.h"
#include "earray.h"
#include "qsortG.h"
#include "utilitiesLib.h"
#include "gimmeDLLWrapper.h"
#include "strings_opt.h"
#include "UnitSpec.h"

typedef struct MemLogData
{
	char *filename;
	S64 bytes;
	S64 count;
	S64 first_alloc;
	S64 last_alloc;
} MemLogData;

static void parseFile(const char *memlog_filename, StashTable memlog_hash, MemLogData ***data_array)
{
	char buffer[2048];
	FILE *f = fopen(memlog_filename, "rt");

	if (!f)
		return;

	while (fgets(buffer, sizeof(buffer), f))
	{
		char *filename, *alloc_count, *address, *bytes, *s;
		S64 alloc_count_i, bytes_i;
		MemLogData *data;

		if (strStartsWith(buffer, " Data: ") || strStartsWith(buffer, "Dumping objects"))
			continue;

		filename = buffer;

		if (!(alloc_count = strstr(filename, " : {")))
			continue;
		*alloc_count = 0;
		alloc_count += 4;

		if (!(s = strstr(alloc_count, "}")))
			continue;
		*s = 0;
		s++;
		if (!(address = strstr(s, " at ")))
			continue;
		address += 4;

		if (!(bytes = strstr(address, ", ")))
			continue;
		*bytes = 0;
		bytes += 2;

		if (!(s = strstr(bytes, " bytes")))
			continue;
		*s = 0;

		while (s = strchr(filename, '\\'))
			filename = s+1;

		alloc_count_i = atoi64(alloc_count);
		bytes_i = atoi64(bytes);

		if (!stashFindPointer(memlog_hash, filename, &data))
		{
			data = calloc(1, sizeof(MemLogData));
			data->filename = strdup(filename);
			data->first_alloc = data->last_alloc = alloc_count_i;
			stashAddPointer(memlog_hash, data->filename, data, false);
			eaPush(data_array, data);
		}

		data->bytes += bytes_i;
		data->count++;
		MIN1(data->first_alloc, alloc_count_i);
		MAX1(data->last_alloc, alloc_count_i);
	}

	fclose(f);
}

static void freeData(MemLogData *data)
{
	free(data->filename);
	free(data);
}

static int cmpData(const MemLogData **ppData1, const MemLogData **ppData2)
{
	const MemLogData *pData1 = *ppData1;
	const MemLogData *pData2 = *ppData2;
	int t;

	t = pData1->bytes - pData2->bytes;
	if (t)
		return t;

	t = pData1->count - pData2->count;
	if (t)
		return t;

	return stricmp(pData1->filename, pData2->filename);
}


int main(int argc, char **argv)
{
	StashTable memlog_hash;
	MemLogData **data_array = NULL;
	char filename[MAX_PATH];
	S64 total_bytes = 0, total_count = 0;
	int i;

	gbSurpressStartupMessages = true;
	gimmeDLLDisable(true);

	DO_AUTO_RUNS;

	fileAutoDataDir();

	// do stuff here
	memlog_hash = stashTableCreateWithStringKeys(2048, StashDefault);
	if (argc>1)
		strcpy(filename, argv[1]);
	else
		strcpy(filename, "memlog.txt");
	parseFile(filename, memlog_hash, &data_array);
	changeFileExt(filename, "SA.txt", filename);
	parseFile(filename, memlog_hash, &data_array);
	stashTableDestroy(memlog_hash);

	eaQSortG(data_array, cmpData);

#define FMT1 "%-12s%-10s%s\n"
#define FMT2 "%-12I64d%-10I64d%s\n"

	printf(FMT1, "Bytes", "Count", "Call location");
	for (i = 0; i < eaSize(&data_array); ++i)
	{
		MemLogData *data = data_array[i];
		if (strcmp(data->filename, "Unused Small Alloc(0)")==0)
			continue;
		total_bytes += data->bytes;
		total_count += data->count;
		printf(FMT2, data->bytes, data->count, data->filename);
	}
	printf("Alloced %s in %I64d chunks\n", friendlyBytes(total_bytes), total_count);

	eaDestroyEx(&data_array, freeData);

	return 0;
}
