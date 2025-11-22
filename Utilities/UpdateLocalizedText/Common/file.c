/* File file.c
 *	7/30/01:
 *		Added ability to have files in the local game data directory override
 *		files in the remote (main) game data directory.  
 *
 *		If a c:\gamedatadir.txt is available, it is expected that each possible 
 *		game data directory be listed in their own line.  Otherwise, an environmental
 *		variable named "GameDataDir" will be used as the paths.  These paths should
 *		be deliminated by semi-colons.
 *
 *		In either format, the last listed directory will be returned by any calls to 
 *		fileDataDir().  Practically speaking, it is a good idea to keep the path to 
 *		the game directory on the server last, so that anything that is still dependent 
 *		on fileDataDir() will continue to write to the main game data directory.
 *
 *		Note since both read/write operations will operate on the local copy if available,
 *		having multiple "local" game data directories may be a bad idea.  It might become
 *		very hard/confusing to find out which files were modified by the game.
 *
 *
 */

#include <stdlib.h> 
#include <string.h>
#include "file.h"
#include "stdtypes.h"
#include "utils.h"
#include <stdarg.h>
#include "StringTable.h"
#include <assert.h>
#include "Common.h"
#include "HashTable.h"
#include "fileutil.h"
#include "logger.h"
#include "fileutil.h"

#ifdef CLIENT
#include "cmdgame.h"
extern GameState game_state;
#endif

// A table of possible strings to try when using relative paths.
StringTable gameDataDirs = 0;
static int loadedGameDataDirs = 0;
static int addSearchPath = 1;
char* mainGameDataDir = NULL;


void fileLoadGameDataDir(int load){
	loadedGameDataDirs = !load;
	addSearchPath = load;
}

int fileAddSearchPath(char* path){
	int createdGameDataDirs = 0;
	int overrideGameDataDirs;

	if(!dirExists(path))
		return 0;

	if (!gameDataDirs)
	{
		gameDataDirs = createStringTable();
		initStringTable(gameDataDirs, 1024);
		strTableSetMode(gameDataDirs, Indexable);//mm
		createdGameDataDirs = 1;
	}

	overrideGameDataDirs = dirExists("./data");
	if(overrideGameDataDirs){
		if(createdGameDataDirs)
			strTableAddString(gameDataDirs, "./data");
	}else
		strTableAddString(gameDataDirs, "./data");

	// loadedGameDataDirs is not altered here on purpose.
	// This is so that additional paths can be added as higher priority
	// search paths before other data dirs are loaded.
	return 1;
}

/* Function fileLoadDataDirs
 *	Loads a list of possible directories from c:\gamedatadir.txt.  It is assumed
 *	that each directory occupies an entire line.  fileLocate() then uses this list 
 *	to find files with relative paths.  The last directory listed in the file is
 *	assumed to be the main game directory, which will be returned by fileDataDir()
 *	to maintain compatibility for some utility programs.  All other directories
 *	are considered "local".
 *	
 *	Note that the environmental variables are now ignored.  While it is possible
 *	to parse a list of directory names that are all concatenated on the same line,
 *	it is not implemented.
 *
 *	
 */
static void fileLoadDataDirs(int forceReload)
{
FILE	*file;
char	*s;
int		use_default = 0;

	// Forcing reload?
	if(forceReload && !gameDataDirs){
		// Destroy all prevously loaded directory names.
		destroyStringTable(gameDataDirs);
		gameDataDirs = 0;
	}

	if(dirExists("./data")){
		fileAddSearchPath("./data");
		loadedGameDataDirs = 1;
		return;
	}

	if(dirExists("./data")){
		fileAddSearchPath("./data");
		loadedGameDataDirs = 1;
		return;
	}

	// Only load directories if none are already loaded.
	if (!gameDataDirs)
	{
		gameDataDirs = createStringTable();
		initStringTable(gameDataDirs, 1024);
		strTableSetMode(gameDataDirs, Indexable);
	}

	{
		char	buffer[1024];

		// Try loading directory names from some text file
		file = fopen("c:/gamedatadir.txt","rt");
		if (file)
		{
			int		len;	
			
			// Extract all directory names and add them to the string table.
			while(fgets(buffer,sizeof(buffer),file)){
				// Reformat the directory name.
				len = strlen(buffer) - 1;
				if (len <= 0 || buffer[0] == '#' || buffer[0] == '\n')
					continue;
				if (len >= 0 && buffer[len] == '\n')
					buffer[len] = 0;

				for(s=buffer;*s;s++)
				{
					if (*s == '\\')
						*s = '/';
				}
				
				// The last seen game data directory will be used as the
				// "main" game data directory.  All other directories will
				// be considered "local".
				mainGameDataDir = (char*)strTableAddString(gameDataDirs, buffer);
			}
			fclose(file);
		}
	}

	loadedGameDataDirs = 1;
}

// Deprecated.
// Use fileLocate().
char *fileDataDir()
{

// Minimal compatibility implementation. =)

#ifdef GETTEX
	char * datadir = "N:\\game\\data";
	return datadir; //to do: why this necessary?
#else
	if (!loadedGameDataDirs)
		fileLoadDataDirs(0);
#endif
	
	return mainGameDataDir;
}

char *fileTempDir()
{
static char tmp_dir[200];
char		*s;

	strcpy(tmp_dir,fileDataDir()); 

	s = strrchr(tmp_dir,'/');
	if (!s)
		s = strrchr(tmp_dir,'\\');
	strcpy(s,"/tmp");
	return tmp_dir;
}

char *fileSrcDir()
{
static char tmp_dir[200];
char		*s;

	strcpy(tmp_dir,fileDataDir());
	s = strrchr(tmp_dir,'/');
	if (!s)
		s = strrchr(tmp_dir,'\\');
	strcpy(s,"/src");
	return tmp_dir;
}

char *fileFixSlashes(char *str)
{
char	*s;

	for(s=str;*s;s++)
		if (*s == '/')
			*s = '\\';

	return str;
}

char *fileFixUpName(char *src,char * tgt)
{
char	*s;
char    *t;

	for(s=src, t=tgt ; *s ; s++,t++)
	{
		*t = toupper(*s);
		if (*s == '\\')
			*t = '/';
	}
	*t = 0;
	if(src[0] == '/')
		return tgt + 1;
	else
		return tgt;
}


/* Function fileLocate()
 *	Locates a file with the relative path.  It is possible for the game directory
 *	to exist in multiple locations.  This function searches through all possible
 *	locations in the order specified in c:\gamedatadir.txt for the file specified
 *	by the given relative path.  If an abosolute path is given, the path is
 *	returned unaltered.
 *
 *
 */
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <direct.h>
static char	fileLocateBuf[1000];
static char* fileLocateSearchTarget;
static int fileLocateSaveLast;

static int fileLocateHelper(char* path)
{
	int success;
	
	// Construct the full path of the possible target file
	sprintf(fileLocateBuf, "%s/%s", path, fileLocateSearchTarget);

	// If the file was not found, continue searching
	if (fileLocateBuf[strlen(fileLocateBuf)-1] == '/')
		success = dirExists(fileLocateBuf);
	else
		success = fileExists(fileLocateBuf);
		
	if(!success && !fileLocateSaveLast){
		fileLocateBuf[0] = '\0';
	}
	
	return !success;
}

static HashTable fileht = 0;
static char datadirs[10][256];
static int datadircount;
static char full_path[512];
static char relative_path[512];
static int builtfiletable = 0;

static FileScanAction fileTableBuilder(char* dir, struct _finddata_t* data)
{
	char * dbl_slash;
	char buf[256];
	int suc, i, len, directory_idx = -1;
	
	if(!(data->attrib & _A_SUBDIR))
	{
		sprintf(relative_path, ""); //debug
		directory_idx = -1;

		sprintf(full_path, "%s/%s", dir, data->name);
		fileFixUpName(full_path, full_path);

		//fix the problem with fileScanAllDataDirs that I don't want to debug now, but gives // paths sometimes:
		dbl_slash = strstr(full_path, "//");
		if(dbl_slash)
		{
			strcpy(buf, dbl_slash+1);
			strcpy(dbl_slash, buf);
		}

		//full_path has no notion of how it was built, so you need to 
		//figure what the relative_path was, so you can hash it and use it as the key
		for(i = 0 ; i < datadircount ; i++)
		{
			len = strlen(datadirs[i]);

			if(!strnicmp(full_path, datadirs[i], len))
			{
				directory_idx = i;
				if(full_path[len] == '/')
					len++;
				strcpy(relative_path, &(full_path[len]));
				break;
			}
		}

		assert(relative_path[0] && directory_idx != -1);
		assert(!hashFindElement(fileht, relative_path));

		suc = hashAddElement(fileht, relative_path, (void *)(directory_idx+1));

		assert(suc);
	}

	return FSA_EXPLORE_DIRECTORY;
}

static void fileBuildFileTable()
{
	int i;
	const char * s;

	printf("building list of data files....");
	if(fileht)
		destroyHashTable(fileht);
	fileht = createHashTable();
	initHashTable(fileht, 100000);
	hashSetMode(fileht, FullyAutomatic); 

	for(i = 0 ; i < 10 ; i++)
	{
		s = strTableGetString(gameDataDirs, i);
		if(s)
		{
			strcpy(datadirs[i], s);
			datadircount++;
		}
		else
			break;
	}

	fileScanAllDataDirs("", fileTableBuilder);
	printf("done\n"); 
	builtfiletable = 1;
}

char *fileLocateQuick(char * fname)
{
	char fixedfname[512];
	int dir_idx;

#ifndef CLIENT
	return fileLocate(fname); //mm
#else
	if(game_state.fxdebug)
		return fileLocate( fname );		//REMOVE ME!! put in @ Woomers suggestion to keep things workin'
#endif

	if (!fname)
		return 0;
	if (fname[1] == ':' || fname[0] == '.')
		return fname;

	if (!loadedGameDataDirs)
		fileLoadDataDirs(0);

	if(!builtfiletable)
		fileBuildFileTable();

	fname = fileFixUpName(fname, fixedfname);
	
	dir_idx = (int)hashFindValue(fileht, fname);
	if(dir_idx)
	{
		sprintf(fileLocateBuf, "%s/%s", datadirs[dir_idx-1], fname);  
		return fileLocateBuf;
	}
	return 0;
}


char *fileLocate(char *fname)
{
	if(0)
	{	
		static char buf[256];
		if(fname[1] == ':')
			return fname;
		else
		{
			strcpy(buf, "N:\\game\\data\\");
			strcat(buf, fname);
			return buf;
		}
	}
	if (!loadedGameDataDirs) 
		fileLoadDataDirs(0);

	if (!fname || !fname[0])
		return 0;

	// Is the given filename an absolute path?
	//	Consider relative paths of the form "./bla bla" and "../blabla"
	//	"absolute" paths also.  In these cases, the filename is being explicitly
	//	specified and does not need "gameDataDirs" to resolve it into a real filename.
	if (fname[1] == ':' || fname[0] == '.')
	{
		//if (mainGameDataDir[1] != ':')
		//	fname += 3;
		strcpy(fileLocateBuf,fname);
		return fileLocateBuf;
	}
	else
	{
		// filename is a relative path
		while('\\' == *fname || '/' == *fname)
			fname++;
		fileLocateSearchTarget = fname;
		fileLocateSaveLast = 1;
		strTableForEachString(gameDataDirs, fileLocateHelper);
		return fileLocateBuf;
	}
}

char *fileLocateExists(char *fname)
{
	if (!loadedGameDataDirs) 
		fileLoadDataDirs(0);

	if (!fname || !fname[0])
		return 0;

	// Is the given filename an absolute path?
	//	Consider relative paths of the form "./bla bla" and "../blabla"
	//	"absolute" paths also.  In these cases, the filename is being explicitly
	//	specified and does not need "gameDataDirs" to resolve it into a real filename.
	if (fname[1] == ':' || fname[0] == '.')
	{
		//if (mainGameDataDir[1] != ':')
		//	fname += 3;
		strcpy(fileLocateBuf,fname);
		return fileLocateBuf;
	}
	else
	{
		// filename is a relative path
		while('\\' == *fname || '/' == *fname)
			fname++;
		fileLocateSearchTarget = fname;
		fileLocateSaveLast = 0;
		strTableForEachString(gameDataDirs, fileLocateHelper);

		return fileLocateBuf[0] ? fileLocateBuf : NULL;
	}
}
//*/
static char file_open_name[1000][256];
static FILE * file_open_ptr[1000];
static file_open_count = 0;

static FileScanAction fileLoadCallback(char* dir, struct _finddata_t* data)
{
	char buf[256];
//	int i,len;

	if(!(data->name[0] == '_') && data->name[0] )
	{
		sprintf(buf, "%s/%s", dir, data->name);
		/*fileFixUpName(buf, buf);

		for(i = 0 ; i < datadircount ; i++)
		{
			len = strlen(datadirs[i]);
			if(!strnicmp(full_path, datadirs[i], len))
			{
				if(buf[len] == '/')
					len++;
				strcpy(file_open_name[file_open_count], &(buf[len]));
				break;
			}
		}*/
		fileFixUpName(buf, file_open_name[file_open_count]);
		file_open_ptr[file_open_count] = fopen(file_open_name[file_open_count],"rbz");
		file_open_count++;
	}
	return FSA_EXPLORE_DIRECTORY;
}

void fileOpenFiles()
{
	printf("pre-file open ....");
	fileScanAllDataDirs("player_library", fileLoadCallback);
	fileScanAllDataDirs("ent_types", fileLoadCallback);
	printf("done (%d)", file_open_count);
}

FILE *fileOpen(char *fname,char *how)
{
	FILE*	file = NULL;
	char*	realFilename;
	int i;

	realFilename = fileLocateQuick(fname); //mm

	if(realFilename && realFilename[0])
	{
		for(i = 0 ; i < file_open_count ; i++)
		{
			if( !stricmp( realFilename, file_open_name[i] ) )
				return file_open_ptr[i];
		}
		file = fopen(realFilename,how);
	}
#if SERVER
	if (strchr(how,'w'))
		log_printf("groupmod.log","fileOpen returned %d for %s",file,fname);
#endif
	return file;
}

/* Function fileOpenEx
 *	Constructs a relative filename using the given formating information
 *	before attempting to locate and open the file.
 *
 *	Users no longer have to manually construct the relative path before
 *	calling fileOpen.
 */
FILE *fileOpenEx(const char *fnameFormat, char *how, ...)
{
va_list va;
char buf[1024];

	// Construct a relative path according to the given filename format.
	va_start(va, how);
	vsprintf(buf, fnameFormat, va);
	va_end(va);

	return fileOpen(buf, how);
}

FILE *fileOpenTemp(char *fname,char *how)
{
char	buf[1000];

	sprintf(buf,"%s/%s",fileTempDir(),fname);
	return fopen(buf,how);
}

void fileFree(void *mem)
{
	free(mem);
}

static char	**names=0;

char **fileScanDirRecurse(char *dir,int *count_ptr)
{
struct _finddata_t fileinfo;
int		handle,test;
char	buf[1200];

	strcpy(buf,fileLocate(dir));
	strcat(buf,"/*");

	for(test = handle = _findfirst(buf,&fileinfo);test >= 0;test = _findnext(handle,&fileinfo))
	{
		if (fileinfo.name[0] == '.' || fileinfo.name[0] == '_')
			continue;
		sprintf(buf,"%s/%s",dir,fileinfo.name);
		if (fileinfo.attrib & _A_SUBDIR)
		{
			fileScanDirRecurse(buf,count_ptr);
			continue;
		}
		names = realloc(names,(*count_ptr+1) * sizeof(names[0]));
		names[*count_ptr] = malloc(strlen(buf)+1);
		strcpy(names[*count_ptr],buf);
		(*count_ptr)++;
	}
	_findclose(handle);
	return names;
}

/*given a directory, it returns an string array of the full path to each of the files in that directory,
and it fills count_ptr with the number of files found.  files and sub-folders prefixed with "_" or "." 
are ignored.  Note: Jonathan's fileLocate is used, so if dir is absolute "C:\..", it's used unchanged, but 
if dir is relative, it will use the gamedir thing.  
*/
char **fileScanDir(char *dir,int *count_ptr)
{
	names=0;
	*count_ptr = 0;
	return fileScanDirRecurse(dir,count_ptr);
}

void fileScanDirFreeNames(char **names,int count)
{
int		i;

	for(i=0;i<count;i++)
		free(names[i]);
	free(names);
}

void fileScanDirRecurseEx(char* dir, FileScanProcessor processor){
	struct _finddata_t fileinfo;
	int	handle,test;
	char buffer[1024];
	FileScanAction action;

	// Relative paths with unspecified roots will work if the directory can be
	// found under one of the game data directories.
	strcpy(buffer, fileLocate(dir));
	strcat(buffer, "/*");

	for(test = handle = _findfirst(buffer, &fileinfo); test >= 0; test = _findnext(handle, &fileinfo)){

		if(fileinfo.name[0] == '.')
			continue;
		
		action = processor(dir, &fileinfo);
		if(	action & FSA_EXPLORE_DIRECTORY && 
			fileinfo.attrib & _A_SUBDIR){

			sprintf(buffer, "%s/%s", dir, fileinfo.name);
			fileScanDirRecurseEx(buffer, processor);
		}

		if(action & FSA_STOP)
			break;
	}
	_findclose(handle);
}



FileScanAction printAllFileNames(char* dir, struct _finddata_t* data){
	printf("%s/%s\n", dir, data->name);
	return FSA_EXPLORE_DIRECTORY;
}

void *fileAlloc_dbg(char *fname,int *lenp MEM_DBG_PARMS)
{
FILE	*file;
int		/*bytes_read,*/total=0;
char	*mem=0,*located_name;
#define CHUNK_SIZE 65536
static char	chunk_buf[CHUNK_SIZE];

	located_name = fileLocateQuick(fname);
	if (!located_name)
		return 0;
	file = fopen(located_name,"rb");
	if (!file)
		return 0;
	//loadUpdate(fname);
#if 1
	fseek(file,0,2);
	total = ftell(file);
	mem = smalloc(total+1);
	fseek(file, SEEK_SET, 0);
	fread(mem,total,1,file);
#else
	for(;;)
	{
		bytes_read = fread(chunk_buf,1,CHUNK_SIZE,file);
		mem = realloc(mem,total + bytes_read + 1);
		memcpy(mem + total,chunk_buf,bytes_read);
		total += bytes_read;
		if (bytes_read < CHUNK_SIZE)
			break;
	}
#endif
	fclose(file);
	mem[total] = 0;
	if (lenp)
		*lenp = total;
	return mem;
}

#if USEZLIB
#include "zlib.h"
#undef getc
#undef fseek
#undef fopen
#undef fclose
#undef ftell
#undef fread
#undef fwrite
#undef fgets
#undef fprintf
#undef fflush

FileWrapper file_wrappers[32];

int glob_assert_file_err;

void *x_fopen(char *name,char *how)
{
int			i,gzio = 0;
FileWrapper	*fw=0;
char		fopenMode[128];
char*		modeCursor;

	glob_assert_file_err = 1;
	strcpy(fopenMode, how);

	// If the character 'z' is used to open the file, use zlib for file IO.
	modeCursor = strchr(fopenMode, 'z');
	if(modeCursor){
		int modeLen;

		// Remove z from the fopen mode string to be given to the real fopen.
		modeLen = strlen(fopenMode);
		memmove(modeCursor, modeCursor + 1, modeLen - (modeCursor - fopenMode));

		gzio = 1;
		
	}else
		gzio = 0;

	for(i=0;i<ARRAY_SIZE(file_wrappers);i++)
	{
		if (!file_wrappers[i].fptr)
		{
			fw = &file_wrappers[i];
			fw->fptr = (void*)1;
			break;
		}
	}
	if (strchr(fopenMode,'w'))
		log_printf("groupmod.log","fw is %d for %s gzio is %d",fw,name,gzio);
	if (!fw)
	{
		glob_assert_file_err = 0;
		return 0;
	}
	if (strchr(fopenMode,'w'))
		log_printf("groupmod.log","x1 %s",name);
	fw->gzio = gzio;
	if (gzio)
	{
		if (strchr(fopenMode,'w'))
			log_printf("groupmod.log","x2 %s",name);
		fw->fptr = gzopen(name,fopenMode);
		if (strchr(fopenMode,'w'))
			log_printf("groupmod.log","x3 %s",name);
	}
	else
	{
		if (strchr(fopenMode,'w'))
			log_printf("groupmod.log","x4 %s",name);
		if (!name || !name[0]) // to avoid dumb assert in msoft's debug c lib
			fw->fptr = 0;
		else
			fw->fptr = fopen(name,fopenMode);
		if (strchr(fopenMode,'w'))
			log_printf("groupmod.log","x5 %s",name);
	}
	if (strchr(fopenMode,'w'))
		log_printf("groupmod.log","fw->fptr is %d for %s",fw->fptr,name);
	if (fw->fptr)
	{
		strcpy(fw->name,name);
		if (strchr(fopenMode,'w'))
			log_printf("groupmod.log","return fw %s",name);
		glob_assert_file_err = 0;

		return fw;
	}
	if (strchr(fopenMode,'w'))
		log_printf("groupmod.log","return fw=0 %s",name);
	glob_assert_file_err = 0;
	return 0;
}

int x_fclose(FileWrapper *fw)
{
void *fptr = fw->fptr;

	fw->fptr = 0;
	memset(fw->name,0,sizeof(fw->name));
	if (fw->gzio)
		return gzclose(fptr);
	else
		return fclose(fptr);
}

void *fileRealPointer(FileWrapper *fw)
{
	if (!fw)
		return 0;
	return fw->fptr;
}

int x_fseek(FileWrapper *fw,long dist,int whence)
{
	if (fw->gzio)
		return gzseek(fw->fptr,dist,whence);
	return fseek(fw->fptr,dist,whence);
}

int x_getc(FileWrapper *fw)
{
	if (fw->gzio)
		return gzgetc(fw->fptr);
	return getc(fw->fptr);
}

long x_ftell(FileWrapper *fw)
{
	if (fw->gzio)
		return gztell(fw->fptr);
	return ftell(fw->fptr);
}

long x_fread(void *buf,long size1,long size2,FileWrapper *fw)
{
	if (!size1)
		return 0;
	if (fw->gzio)
		return gzread(fw->fptr,buf,size1 * size2) / size1;
	return fread(buf,size1,size2,fw->fptr);
}

long x_fwrite(const void *buf,long size1,long size2,FileWrapper *fw)
{
	if (fw->gzio)
		return gzwrite(fw->fptr,(void*)buf,size1 * size2);
	return fwrite(buf,size1,size2,fw->fptr);
}

char *x_fgets(char *buf,int len,FileWrapper *fw)
{
	if (fw->gzio)
		return gzgets(fw->fptr,buf,len);
	return fgets(buf,len,fw->fptr);
}

void x_fflush(FileWrapper *fw)
{
	if (fw->gzio)
		gzflush(fw->fptr,0);
	else
		fflush(fw->fptr);
}

int x_fprintf (FileWrapper *fw, const char *format, ...)
{
char buf[1200];
va_list va;
int len;

    va_start(va, format);
    len = _vsnprintf(buf, sizeof(buf), format, va);
    va_end(va);
    return x_fwrite(buf,len,1,fw);
}

/* Function x_fscanf()
 *	A very basic scanf to be used with FileWrapper.  This function always extracts
 *	one string from the file no matter what "format" says.  In addition, this function
 *	will only compile against VC++, since it relies on one of the VC stdio internal 
 *	functions, _input(), and the structure _iobuf.
 *
 *
 */
/*
extern int __cdecl _input(FILE *, const unsigned char *, va_list);
int x_fscanf(FileWrapper* fw, const char* format, ...){
va_list va;
#define bufferSize 1024
char buffer[bufferSize];
int retval;

	va_start(va, format);

	
	x_fgets(buffer, bufferSize, fw);

	// Adopted from VC++ 6's sscanf.
	{
		struct iobuf str;
		struct iobuf *infile = &str;
		
		_ASSERTE(buffer != NULL);
		_ASSERTE(format != NULL);
		
		infile->_flag = _IOREAD|_IOSTRG|_IOMYBUF;
		infile->_ptr = infile->_base = (char *) buffer;
		infile->_cnt = strlen(buffer);
		
		retval = (_input(infile,format, va));
	}
	
	va_end(va);
	return(retval);
}
*/
#endif

