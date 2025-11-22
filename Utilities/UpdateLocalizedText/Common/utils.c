#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "utils.h"
//#include "Common.h"

#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <direct.h>
#include <errno.h>
#include <assert.h>
#include "file.h"

/* is refname older than testname?
*/
int fileNewer(char *refname,char *testname)
{
	struct _stat refbuf,testbuf;
	int refres,testres,t;

   refres = _stat(refname,&refbuf);
   testres = _stat(testname,&testbuf);

	if (testres != 0 || refres != 0)
		return 1;

   	t = testbuf.st_mtime - refbuf.st_mtime;
	return t > 0;
}

U32 fileLastChanged(char *refname)
{
	struct _stat refbuf;
	int fail;

   fail = _stat(refname,&refbuf);
   if(!fail)
   		return refbuf.st_mtime;
   else
		return 0;
}

void forwardSlashes(char *path)
{
	char	*s;

	for(s=path;*s;s++)
	{
		if (*s == '\\')
			*s = '/';
	}
	
}

void backSlashes(char *path)
{
	char	*s;

	for(s=path;*s;s++)
	{
		if (*s == '/')
			*s = '\\';
	}
	
}

void concatpath(char *s1,char *s2,char *full)
{
	char	*s;

	if (s2[1] == ':' || s2[0] == '/')
	{
		strcpy(full,s2);
		return;
	}
	strcpy(full,s1);
	s = &full[strlen(full)-1];
	if (*s != '/' && *s != '\\')
		strcat(s,"/");
	strcat(full,s2);
}

void setgamedir(char *fname,char *dirname,char *newfname)
{
	char	*s;

	s = strstri(fname,"game/");
	strncpy(newfname,fname,s - fname + 5);
	newfname[s - fname + 5] = 0;
	strcat(newfname,dirname);
	s = strchr(s + 5,'/');
	if (s)
		strcat(newfname,s);
}

void makefullpath(char *dir,char *full)
{
	char	base[_MAX_PATH];

	_getcwd(base, _MAX_PATH );
	concatpath(base,dir,full);
	forwardSlashes(full);
}

// Given a path to a file, this function makes sure that all directories
// specified in the path exists.
void mkdirtree(char *path)
{
	char	*s;

	s = path;
	for(;;)
	{
		s = strchr(s,'/');
		if (!s)
			break;
		*s = 0;
		mkdir(path);
		*s = '/';
		s++;
	}
}

void rmdirtree(char *path)
{
	char	buf[1000],*s;

	strcpy(buf,path);
	for(;;)
	{
		s = strrchr(buf,'/');
		if (!s)
			break;
		*s = 0;
		_rmdir(buf);
	}
}


// Given a path to a directory, this function makes sure that all directories
// specified in the path exists.
int makeDirectories(char* dirPath)
{
char	*s;

	s = dirPath;

	forwardSlashes(dirPath);
	// Look for the first slash that seperates a drive letter from the rest of the
	// path.
	s = strchr(s,'/');
	if(!s)
		return 0;
	s++;

	for(;;)
	{
		// Locate the next slash.
		s = strchr(s,'/');
		if (!s){
		if(0 != mkdir(dirPath) && EEXIST != errno)
				return 0;
			else
				return 1;
		}

		*s = 0;

		// Try to make the directory.  If the operation didn't succeed and the
		// directory doesn't already exist, the required directory still doesn't
		// exist.  Return an error code.
		if(0 != mkdir(dirPath) && EEXIST != errno)
			return 0;

		// Otherwise, restore the string and continue processing it.
		*s = '/';
		s++;
	}
}


//r = reentrant. just like strtok, but you keep track of the pointer yourself (last)
char *strtok_r(char *target,const char *delim,char **last)
{
int		start;

	if ( target != 0 )
		*last = target;
	else 
		target = *last;
	
	if ( !target || *target == '\0' )
		return 0;

	start = strspn(target,delim);
	target = &target[start];
	if ( *target == '\0' )
	{
		/* failure to find 'start', remember and return */
		*last = target;
		return 0;
    }

    /* found beginning of token, now look for end */
	if ( *(*last = target + strcspn(target,delim)) != '\0')
	{
		*(*last)++ = '\0';
	}
	return target;
}


/* Function strsep2()
 *	Similar to strsep(), except this function returns the deliminator
 *	through the given retrievedDelim buffer if possible.
 *
 */
char* strsep2(char** str, const char* delim, char* retrievedDelim){
	char* token;
	int spanLength;
	
	// Try to grab the token from the beginning of the string
	// being processed.
	token = *str;
	
	// If no token can be found from the string being processed,
	// return nothing.
	if('\0' == *token)
		return NULL;

	// Find out where the token ends.
	spanLength = strcspn(*str, delim);

	// Advance the given string pointer to the end of the current token.
	*str = token + spanLength;

	// Extract the retrieved deliminator if requested.
	if(retrievedDelim)
		*retrievedDelim = **str;

	// If the end of the string has been reached, the string pointer is
	// pointing at the NULL terminating character.  Return the extracted 
	// token.  The string pointer will be left pointing to the NULL 
	// terminating character.  If the same string pointer is passed 
	// back in a later call to this function, the function would return 
	// NULL immediately.
	if('\0' == **str)
		return token;
	
	// Otherwise, the string pointer is pointing at a deliminator.  Turn
	// it into a NULL terminating character to mark the end of the token.
	**str = '\0';
	
	// Advance the string pointer to the next character in the string.
	// The string can continue to be processed if the same string pointer
	// is passed back later.
	(*str)++;
	
	return token;
}

/* Function strsep()
 *	A re-entrant replacement for strtok, similar to strtok_r.  Given a cursor into a string,
 *	and a list of deliminators, this function returns the next token in the string pointed
 *	to by the cursor.  The given cursor will be forwarded pass the found deliminator.
 *
 *	Note that the owner string pointer should not be passed to this function.  Doing so may
 *	result in memory leaks.  This function will alter the given string pointer (cursor).
 *
 *	Moved from entScript.c
 *
 *	Parameters:
 *		str - the cursor into the string to be used.
 *		delim - a list of deliminators to be used.		
 *
 *	Returns:
 *		Valid char* - points to the next token in the string.
 *		NULL - no more tokens can be retrieved from the string.
 */
char* strsep(char** str, const char* delim){
	return strsep2(str, delim, NULL);
}

#ifndef strcasecmp
int strcasecmp(const char *a,const char *b)
{
	if (!a) return -1;
	if (!b) return 1;
	for(;*a && *b && tolower(*a) == tolower(*b);a++,b++) ;
	return tolower(*a) - tolower(*b);
}
#endif

int strncasecmp(const char *a,const char *b,int n)
{
	if (!a) return -1;
	if (!b) return 1;
	for(;n > 1 && *a && *b && tolower(*a) == tolower(*b);a++,b++,n--) ;
	return tolower(*a) - tolower(*b);
}

char *strstri(char *s,char *srch)
{
	int		len = strlen(srch);

	for(;*s;s++)
	{
		if (strncasecmp(s,srch,len)==0)
			return s;
	}
	return 0;
}

char *fileLoadSize(char *fname,unsigned long *size)
{
	FILE	*file;
	int		len;
	char	*mem;

	file = fopen(fname,"rb");
	if (!file)
		return 0;
	fseek(file,0,2);
	len = ftell(file);
	mem = malloc(len + 1);
	fseek(file,0,0);
	len = fread(mem,1,len,file);
	fclose(file);
	mem[len] = 0;
	if (size)
		*size = len;
	return mem;
}

char *fileLoad(char *fname)
{
	return fileLoadSize(fname,0);
}

int fileSize(char *fname){
	struct stat status;

	if(!stat(fname, &status)){
		if(!(status.st_mode & _S_IFREG))
			return -1;
		return status.st_size;
	}
	
	// The file doesn't exist.
	return -1;
}

int fileExists(char *fname){
	struct stat status;

	if(!stat(fname, &status)){
		if(status.st_mode & _S_IFREG)
			return 1;
	}
	
	return 0;
}

int dirExists(char *dirname){
	struct stat status;
	char	*s,buf[1000];

	strcpy(buf,dirname);
	s = &buf[strlen(buf)-1];
	if (*s == '/')
		*s = 0;
	if(!stat(buf, &status)){
		if(status.st_mode & _S_IFDIR)
			return 1;
	}
	
	return 0;
}

#include <process.h>

int tokenize_line(char *buf,char *args[],char **next_line_ptr)
{
	char	*s,*next_line;
	int		i,idx;

	s = buf;
	for(i=0;;)
	{
		args[i] = 0;
		if (*s == ' ')
			s++;
		else if (*s == 0)
		{
			next_line = 0;
			break;
		}
		else if (*s == '"')
		{
			args[i++] = s+1;
			s = strchr(s+1,'"');
			*s++ = 0;
		}
		else
		{
			if (*s != '\r' && *s != '\n')
				args[i++] = s;
			idx = strcspn(s,"\n ");
			s += idx;
			if (*s == ' ')
				*s++ = 0;
			else
			{
				if (*s == 0)
					next_line = 0;
				else
				{
					*s = 0;
					if (s[-1] == '\r')
						s[-1] = 0;
					next_line = s+1;
				}
				args[i] = 0;
				break;
			}
		}
	}
	if (next_line_ptr)
		*next_line_ptr = next_line;
	return i;
}

int system_detach(char *cmd)
{
	char	*args[50],buf[1000];

	strcpy(buf,cmd);
	tokenize_line(buf,args,0);
	return _spawnvp( _P_DETACH , args[0], args );
}

void *dynArrayAdd(void **basep,int struct_size,int *count,int *max_count,int num_structs)
{
	char	*base = *basep;

	if (*count >= *max_count -num_structs)
	{
		if (!*max_count)
			*max_count = num_structs;
		(*max_count) <<= 1;
		if (num_structs > 1)
			(*max_count) += num_structs;
		base = realloc(base,struct_size * *max_count);
		memset(base + struct_size * *count,0,(*max_count - *count) * struct_size);
		*basep = base;
	}
	(*count)+=num_structs;
	return base + struct_size * (*count - num_structs);
}

// same as dynArrayAdd, but for arrays of struct pointers instead of arrays of structs
void *dynArrayAddp(void ***basep,int *count,int *max_count,void *ptr)
{
	void	**mem;

	mem = dynArrayAdd((void *)basep,sizeof(void *),count,max_count,1);
	*mem = ptr;
	return mem;
}

void *dynArrayFit(void **basep,int struct_size,int *count_ptr,int *max_count,int idx_to_fit)
{
	int		last_max;
	char	*base = *basep;

	if (idx_to_fit >= *count_ptr)
		*count_ptr = idx_to_fit+1;

	if (idx_to_fit >= *max_count)
	{
		last_max = *max_count;
		*max_count = (*count_ptr) * 2;
		base = realloc(base,struct_size * *max_count);
		memset(base + struct_size * last_max,0,(*max_count - last_max) * struct_size);
		*basep = base;
	}
	return base + struct_size * idx_to_fit;
}

#include <io.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <wininclude.h>
#undef FILE
void newConsoleWindow()
{
	int hCrt,i;
	FILE *hf;

	AllocConsole();
	{
		hCrt = _open_osfhandle(	(long) GetStdHandle(STD_OUTPUT_HANDLE),_O_TEXT);
		hf = _fdopen( hCrt, "w" );
		*stdout = *hf;
		i = setvbuf( stdout, NULL, _IONBF, 0 );
	}

}

void setConsoleTitle(char *msg)
{
	static	char	last_buf[1000];

	if (strcmp(last_buf,msg)==0)
		return;
	strcpy(last_buf,msg);
	SetConsoleTitle(msg);
}

char *getFileName(char *fname)
{
		char	*s;

	s = strrchr(fname,'/');
	if (!s)
		s = strrchr(fname,'\\');
	if (!s++)
		s = fname;
	return s;
}

/* getDirectoryName()
 *	Given a path to a file, this function returns the directory name
 *	where the file exists.
 *
 *	Note that this function will alter the given string to produce the
 *	directory name.
 */
char *getDirectoryName(char *fullPath)
{
	char	*cursor;

	cursor = strrchr(fullPath,'/');
	if (!cursor)
		cursor = strrchr(fullPath,'\\');
	if (!cursor)
		cursor = NULL;
	else
		*cursor = '\0';
	if(strlen(fullPath) == 2 && fullPath[1] == ':'){
		fullPath[2] = '\\';
		fullPath[3] = '\0';
	}
	return fullPath;
}

char *getUserName()
{
	static	char	name[1000];
	int		name_len = sizeof(name);

	GetUserName(name,&name_len);
	return name;
}
