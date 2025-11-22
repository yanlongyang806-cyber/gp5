#include "fileutil.h"
#include "sysutil.h"
#include "hoglib.h"
#include "piglib.h"
#include "patchfile.h"
#include "bindiff.h"
#include "patchcommonutils.h"
#include "pcl_typedefs.h"
#include "timing.h"
#include "earray.h"
#include "gimmeDLLWrapper.h"
#include "utils.h"
#include "cmdparse.h"
#include "zutils.h"
#include "file.h"
#include "zlib.h"
#include "utilitiesLib.h"
#include "patcher_comm.h"
#include "utils.h"
#include "windefinclude.h"
#include "utf8.h"

#pragma warning (disable:6294) // Ill-defined for-loop: initial condition does not satisfy test. Loop body not executed
U32	g_extra;
bool g_humanbytes = true;
AUTO_CMD_INT(g_humanbytes, humanbytes);
bool g_shownew = true;
AUTO_CMD_INT(g_shownew, shownew);
bool g_showold = true;
AUTO_CMD_INT(g_showold, showold);
char g_source[MAX_PATH] = {0};
AUTO_CMD_STRING(g_source, source);
char g_dest[MAX_PATH] = {0};
AUTO_CMD_STRING(g_dest, dest);
bool g_nopatch = false;
AUTO_CMD_INT(g_nopatch, nopatch);
bool g_memzip = false;
AUTO_CMD_INT(g_memzip, memzip);
bool g_forcecompressed = true;
AUTO_CMD_INT(g_forcecompressed, forcecompressed);

typedef struct FileStats2{
	char name[MAX_PATH];
	U32 total;
	U32 trans;
	bool new, folder;
} FileStats2;

static F32 *speeds = NULL;
static U64 trans = 0;
static U64 total_bytes = 0;
static U64 raw_diff_bytes = 0;
static U32 t = 0;

// From patchfile.c
static U32 print_size[] = { 64*SMALLEST_DATA_BLOCK,
							16*SMALLEST_DATA_BLOCK,
							 4*SMALLEST_DATA_BLOCK,
							   SMALLEST_DATA_BLOCK };

static void display_stats(DiffStats *stats)
{
	//int i;
	//printf("t=%u first=%u second=%u\n", stats->checksums, stats->miss_first, stats->miss_second);
	//printf("hist="); for(i=0; i<256; i++) printf("%u ", stats->checksum_hist[i]); printf("\n");
}

static void freeFileData(PatchFileData *d)
{
	int i;
	for(i = 0; i < d->num_print_sizes; i++)
		SAFE_FREE(d->prints[i]);
	SAFE_FREE(d->data);
	SAFE_FREE(d);
}

typedef struct RunBindiffData {
	HogFile *handle;
	FileStats2 ***stats_arr;
	U32 total_files, cur_file;
} RunBindiffData;

U32	hogFileSmallestSize(HogFile *handle, HogFileIndex index)
{
	U32		raw,packed;

	hogFileGetSizes(handle, index, &raw, &packed);

	if (packed)
		return packed;
	return raw;
}

static struct z_stream_s	z_send;

static void *my_zcalloc(void *opaque,uInt num,uInt size)
{
	return calloc(num,size);
}

static void my_zfree(void *opaque,void *data)
{
	free(data);
}


static void compressInit()
{
static int init;

	if (init)
		return;
	init = 1;
	z_send.zalloc = my_zcalloc;
	z_send.zfree = my_zfree;
	deflateInit(&z_send,4);
}

static int addToZipStream(void *data,int size)
{
	z_stream	*z		= &z_send;
	char		*zdata;
	int			zsize,zmaxsize;

	zmaxsize = deflateBound(z, size);
	zdata = malloc(zmaxsize);
	z->avail_in		= size;
	z->next_in		= data;
	z->avail_out	= zmaxsize;
	z->next_out		= zdata;
	{
		deflate(z,1);
	}
	assert(!z->avail_in);
	zsize = zmaxsize - z->avail_out;

	free(zdata);
	return zsize;
}


static	U8	*xfer_buf;
static	int	xfer_total,xfer_max;
#define MIN_FILE_DIFF_SIZE 8192

int z_input_total,z_output_total;

/* stats
65536		= 257
32768		= 256
16384		= 255.6
8192		= 254
4096		= 253
*/

static bool runBindiff(HogFile *dest_handle, HogFileIndex dest_index, const char* filename, RunBindiffData *userData)
{
	HogFile *handle = userData->handle;
	HogFileIndex index = hogFileFind(handle, filename);
	int i, print_idx, num_block_reqs;
	PatchFileData *source, *dest, *new;
	U8 *old_copied;
	U32 *fingerprints, *scoreboard, max_contiguous, sub_count=1, next_sub_count, *block_reqs = NULL, num_prints;
	DiffStats *stats = NULL;
	U32 bindiff_timer;
	F32 elapsed = 0;
	FileStats2 *file_stats = calloc(1, sizeof(FileStats2));
	int raw_diff = 0;
	int	zsize=0;

	compressInit();

	userData->cur_file += 1;
	setConsoleTitle(STACK_SPRINTF("%.2f%% %u / %u", (100.0 * userData->cur_file / userData->total_files), userData->cur_file, userData->total_files));

	source = callocStruct(PatchFileData);
	dest = callocStruct(PatchFileData);
	new = callocStruct(PatchFileData);

	strcpy(file_stats->name, filename);

	{
		HogFileIndex test = hogFileFind(dest_handle, filename);
		assert(test == dest_index);
	}
	
	dest->len = hogFileSmallestSize(dest_handle, dest_index);
	total_bytes += dest->len;
	file_stats->total = dest->len;

	if(index == -1)
	{
		// Not found in both, move on. New file, means we transfer the whole thing.
		//printf("%s not found in source\n", filename);
		raw_diff_bytes += dest->len;
		file_stats->new = true;
		goto GetWholeFile;

	}
	source->len = hogFileSmallestSize(handle, index);

	if(source->len == dest->len && 
	   /*hogFileGetFileTimestamp(handle, index) == hogFileGetFileTimestamp(dest_handle, dest_index) &&*/
		hogFileGetFileChecksum(handle, index) == hogFileGetFileChecksum(dest_handle, dest_index))
	{
		// Files match
		eaPush(userData->stats_arr, file_stats);
		freeFileData(source);
		freeFileData(dest);
		freeFileData(new);
		return true;
	}

	if (dest->len < 1000000 && dest->len >= MIN_FILE_DIFF_SIZE && !g_forcecompressed)
	{
		raw_diff = 1;
		source->data = hogFileExtract(handle, index, &source->len, NULL);
		dest->data = hogFileExtract(dest_handle, dest_index, &dest->len, NULL);
	}
	else
	{
		source->data = hogFileExtractCompressed(handle, index, &source->len);
		if(!source->data)
		{
			source->data = hogFileExtract(handle, index, &source->len, NULL);
			raw_diff = 1;
		}
		dest->data = hogFileExtractCompressed(dest_handle, dest_index, &dest->len);
		if(!dest->data)
			dest->data = hogFileExtract(dest_handle, dest_index, &dest->len, NULL);
	}
	raw_diff_bytes += dest->len;

	// From patchxfer.c:fileTooDifferent()
	if(source->len > dest->len * 2 || dest->len > source->len * 2 || dest->len < MIN_FILE_DIFF_SIZE)
	{
		//printf("%s is too different, requesting entire file (%u -> %u)\n", filename, source->len, dest->len);
		goto GetWholeFile;
	}

	// From patchfile.c:s_patchfiledataInitSizes()
	for(i = 0; i < MAX_PRINT_SIZES-1; i++)
		if(dest->len > print_size[i])
			break;
	dest->num_print_sizes = MAX_PRINT_SIZES - i;
	dest->print_sizes = print_size + i;
	dest->block_len = (dest->len + dest->print_sizes[0]-1) & ~(dest->print_sizes[0]-1);

	// From patchfile.c:s_patchfiledataInitForXfer()
	dest->data = realloc(dest->data, dest->block_len);
	memset(dest->data + dest->len, 0, dest->block_len - dest->len);
	for(i = 0; i < dest->num_print_sizes; i++)
		dest->prints[i] = bindiffMakeFingerprints(dest->data, dest->block_len, dest->print_sizes[i], &dest->num_prints[i]);
	dest->crc = patchChecksum(dest->data, dest->len);

	// From patchxfer.c:receiveFileInfo()
	source->block_len = source->len + (dest->print_sizes[0])-1;
	source->data = realloc(source->data, source->block_len);
	old_copied = calloc( ((source->block_len - 1) >> 3) + 1, 1);
	memset(source->data + source->len, 0, source->block_len - source->len);

	new->data = calloc(dest->block_len, 1);
	fingerprints = calloc(dest->block_len / dest->print_sizes[dest->num_print_sizes-1], sizeof(fingerprints[0]));
	scoreboard = calloc(dest->block_len / dest->print_sizes[dest->num_print_sizes-1], 4);
	
	bindiff_timer = timerAlloc();
	for(print_idx = 0; print_idx < dest->num_print_sizes;)
	{
		
		
		// From patchxfer.c:receiveFingerprints()
		num_prints = dest->block_len / dest->print_sizes[print_idx];

		if (print_idx >= dest->num_print_sizes-1) // requesting raw data
			max_contiguous = MAX_SINGLE_REQUEST_BYTES / dest->print_sizes[print_idx];
		else // requesting next set of fingerprints
		{
			next_sub_count = dest->print_sizes[print_idx] / dest->print_sizes[print_idx+1];
			max_contiguous = MAX_SINGLE_REQUEST_BYTES/ (next_sub_count * sizeof(U32));
		}

		timerStart(bindiff_timer);
		num_block_reqs = bindiffCreatePatchReqFromFingerprints(
			dest->prints[print_idx],dest->num_prints[print_idx],
			source->data,source->block_len,
			new->data,dest->block_len,
			&block_reqs,dest->print_sizes[print_idx],
			scoreboard, old_copied, max_contiguous, 2, &stats);
		elapsed += timerElapsed(bindiff_timer);

		if(num_block_reqs == 0)
			break;
		else if(print_idx >= dest->num_print_sizes-1)
		{
			int total = 0;
			//printf("Transfer %s (", filename);
			for(i=0; i<num_block_reqs; i++)
			{
				U8			*m;

				//printf("%u %u ", block_reqs[i*2], block_reqs[i*2+1]);
				// block_reqs is an array of [start1, count1, start2, count2, ...]
				U32 block_trans = block_reqs[i*2+1] * dest->print_sizes[print_idx];
				trans += block_trans;
				file_stats->trans += block_trans;

				if (raw_diff)
				{
					int	zbytes;
					if (g_memzip)
					{
						m = dynArrayAdd(xfer_buf,1,xfer_total,xfer_max,block_trans);
						memcpy(m,dest->data + block_reqs[i*2] * dest->print_sizes[print_idx],block_trans);
					}

					zbytes = addToZipStream(dest->data + block_reqs[i*2] * dest->print_sizes[print_idx],block_trans);
					zsize += zbytes;
					z_input_total += block_trans;
					z_output_total += zbytes;

				}
			}
			{
				U32 extra = dest->print_sizes[print_idx] - (dest->len & (dest->print_sizes[print_idx]-1));
				if (extra == dest->print_sizes[print_idx])
					extra = 0;
				g_extra += extra;
			}
			//printf(")\n");
			//display_stats(stats);
			break;
		}

		print_idx++;

		sub_count = dest->print_sizes[print_idx-1] / dest->print_sizes[print_idx];

		//xfer->blocks_total *= sub_count;

		for(i = num_prints - 1; (int) i >= 0; i--)
		{
			U32 j;

			if (scoreboard[i])
			{
				for(j=0;j<sub_count;j++)
					scoreboard[i*sub_count+j] = scoreboard[i] + j * dest->print_sizes[print_idx];
			}
			else
			{
				for(j=0;j<sub_count;j++)
					scoreboard[i*sub_count+j] = 0;
			}
		}
	}

	// Clean up
	eaPush(userData->stats_arr, file_stats);
	eafPush(&speeds, dest->len/elapsed);
	free(stats);
	freeFileData(source);
	freeFileData(dest);
	freeFileData(new);
	SAFE_FREE(old_copied);
	SAFE_FREE(fingerprints);
	SAFE_FREE(scoreboard);
	SAFE_FREE(block_reqs);
	timerFree(bindiff_timer);

	goto ziptally;

GetWholeFile:
	// Transfer the entire file
	{
		int size, block_size, num_blocks, extra, block_trans;

		size = dest->len;
		block_size = print_size[ARRAY_SIZE_CHECKED(print_size) - 1];
		num_blocks = (size + block_size - 1) / block_size;
		block_trans = num_blocks * block_size;
		trans += block_trans;
		file_stats->trans = block_trans;

		extra = block_size - (size & (block_size-1));
		if (extra == block_size)
			extra = 0;
		g_extra += extra;

		if (raw_diff && dest->data)
		{
			if (g_memzip)
			{
				U8	*m;

				m = dynArrayAdd(xfer_buf,1,xfer_total,xfer_max,block_trans);
				memset(m,0,block_trans);
				memcpy(m,dest->data,MIN((U32)block_trans,dest->len));
			}

			zsize += addToZipStream(dest->data,MIN((U32)block_trans,dest->len));
			z_input_total += MIN((U32)block_trans,dest->len);
			z_output_total += zsize;
		}

		eaPush(userData->stats_arr, file_stats);
		freeFileData(source);
		freeFileData(dest);
		freeFileData(new);
	}

ziptally:
	if (zsize)
	{
		file_stats->trans  = zsize;
	}

	return true;
}

static int sortFileStats(const FileStats2 **a, const FileStats2 **b)
{
	return (*a)->trans - (*b)->trans;
}

int wmain(int argc, WCHAR** argv_wide)
{
	char **argv;
	HogFile *source_hogg, *dest_hogg;
	int i;
	F32 avg= 0;
	FileStats2 **file_stats=NULL;
	RunBindiffData rbd = {0};
	F32 trans_num, total_num;
	char *trans_units, *total_units;
	U32 trans_prec, total_prec;
	char *diff_str = 0, source_name[MAX_PATH], dest_name[MAX_PATH];
	extern void analyzeDiffs(char *mem);
	float megabyte = 1024*1024;
	char **filenames=NULL;
	rbd.stats_arr = &file_stats;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS

	setCavemanMode();

	cmdParseCommandLine(argc, argv);

	if(!g_source[0] || !g_dest[0])
	{
		printf("Invalid arguments, need two input folders\n");
		exit(-1);
	}

	{
		WIN32_FIND_DATAA find_data = {0};
		HANDLE find_handle;

		find_handle = FindFirstFile_UTF8(STACK_SPRINTF("%s\\*.hogg", g_dest), &find_data);
		if(find_handle != INVALID_HANDLE_VALUE)
		{
			do
			{
				eaPush(&filenames, strdup(find_data.cFileName));
			} 
			while(FindNextFile_UTF8(find_handle, &find_data));
		}
		FindClose(find_handle);
	}
	

	for(i=0; i<eaSize(&filenames); i++)
	{
		sprintf(dest_name, "%s/%s", g_dest, filenames[i]);
		dest_hogg = hogFileRead(dest_name, NULL, PIGERR_ASSERT, NULL, HOG_READONLY|HOG_NOCREATE);
		rbd.total_files += hogFileGetNumFiles(dest_hogg);
		hogFileDestroy(dest_hogg, true);
	}


	for(i=0; i<eaSize(&filenames); i++)
	{
		sprintf(source_name, "%s/%s", g_source, filenames[i]);
		sprintf(dest_name, "%s/%s", g_dest, filenames[i]);
		source_hogg = hogFileRead(source_name, NULL, PIGERR_ASSERT, NULL, HOG_READONLY|HOG_NOCREATE);
		dest_hogg =   hogFileRead(dest_name, NULL, PIGERR_ASSERT, NULL, HOG_READONLY|HOG_NOCREATE);
		if(!source_hogg)
		{
			printf("Hogg %s not found", source_name);
			exit(1);
		}
		if(!dest_hogg)
		{
			printf("Hogg %s not found", dest_name);
			exit(1);
		}
		rbd.handle = source_hogg;
		hogScanAllFiles(dest_hogg, runBindiff, &rbd);
		hogFileDestroy(source_hogg, true);
		hogFileDestroy(dest_hogg, true);
	}

	for(i=0; i<eafSize(&speeds); i++)
	{
		avg += speeds[i]/eafSize(&speeds);
	}

	eaQSort(file_stats, sortFileStats);
	FOR_EACH_IN_EARRAY(file_stats, FileStats2, s)
		if(s->trans && (s->new ? g_shownew : g_showold))
		{
			if(g_humanbytes)
			{
				humanBytes(s->trans, &trans_num, &trans_units, &trans_prec);
				humanBytes(s->total, &total_num, &total_units, &total_prec);
				estrConcatf(&diff_str,"%s: %.*f%s / %.*f%s\n", s->name, trans_prec, trans_num, trans_units, total_prec, total_num, total_units);
			}
			else
				estrConcatf(&diff_str,"%s: %u / %u\n", s->name, s->trans, s->total);
		}
	FOR_EACH_END

	analyzeDiffs(diff_str);

	{
		void	*m;
		int		xfer_zipsize=0;

		if (g_memzip)
		{
			m = zipDataEx(xfer_buf,xfer_total,&xfer_zipsize,9,false,0);
			free(m);
			printf("ZipData  : %4.1fM -> %4.1fM\n",xfer_total/1000000.0,xfer_zipsize/1000000.0);
		}
		printf("ZipStream: %4.1fM -> %4.1fM\n",z_input_total/1000000.0,z_output_total/1000000.0);
	}
	printf("transfer = %.2fMB\traw = %.2fMB\n", trans / megabyte, raw_diff_bytes / megabyte);
	humanBytes(raw_diff_bytes, &trans_num, &trans_units, &trans_prec);
	humanBytes(total_bytes, &total_num, &total_units, &total_prec);
	printf("total = %.2fMB\tBlock overhead = %.2fMB\n", total_bytes / megabyte, g_extra/ megabyte);

	printf("\n\n%s\n",diff_str);
	estrDestroy(&diff_str);
	eaDestroyEx(&filenames, NULL);
	EXCEPTION_HANDLER_END

	return 0;
}
