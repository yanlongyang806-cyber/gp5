#include "scrubutils.h"
#include "timing.h"
#include "earray.h"
#include "estring.h"
#include "file.h"
#include "utils.h"
#include "error.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "cmdparse.h"
#include "renamer.h"
#include "gimmeDLLWrapper.h"
#include "StashTable.h"

#define DRYRUN 0

typedef struct
{
	int		src_len;
	int		dst_len;
	char	*src,*dst;
	int		always_swap : 1;
	int		swap_second : 1;
	int		common_word : 1;
	int		case_sensitive : 1;
} WordSwap;

#define SWAPCMD_MODIFY 1
#define SWAPCMD_DELETE 2
#define SWAPCMD_RENAME 4

typedef struct
{
	char		*src_fname;
	char		*dst_fname;
	int			flags;
	StashTable	allowed_swaps;
} SwapCmd;


WordSwap	**word_swaps;
int			g_findSwaps;
FILE		*swap_file;
FILE		*maxcmd_file;
FILE		*maybe_swaps;
StashTable	swapcmd_hashes;
SwapCmd		**swapcmd_list;
SwapCmd		*curr_swap;
char		**search_dirs;

WordSwap	**word_swap_index[256];

int diffCase(int a,int b)
{
	return (a ^ b) & 64;
}

int checkEnds(int prev_char,char *s,WordSwap *swap)
{
	int		left=0;
	char	l,*rp = &s[swap->src_len];

	if (swap->case_sensitive && strncmp(s,swap->src,swap->src_len)!=0)
		return 0;
	if (prev_char)
	{
		l = s[-1];
		if (!isalpha(l))
			left = 1;
		if (swap->src_len < 4 || swap->common_word)
		{
			if (islower(l) && isupper(*s))
				left = 1;
		}
		else
		{
			if (islower(l) || isupper(*s))
				left = 1;
		}
	}
	else
		left = 1;
	if (!left)
		return 0;

	if (!swap->common_word)
	{
		if (*rp == 's')
			rp++;
		else if (*rp == 'e' && rp[1] == 's')
			rp+=2;
	}
	if (!isalpha(*rp))
		return 1;
	if (isupper(*rp))
		return 1;
	return 0;
}

char *fixWord(char *src,char *buf,int careful,int *maybe)
{
	int			i,modified=0;
	char		*s,*dst = buf;
	WordSwap	*swap;
	int			swapped=0;
	WordSwap	**curr_swaps;

	if (maybe)
		*maybe = 0;

	if (!g_findSwaps)
	{
		char	*dst_name;

		if (curr_swap && stashFindPointer(curr_swap->allowed_swaps,src,&dst_name))
		{
			strcpy_s(buf,strlen(dst_name)+1,dst_name);
			return buf;
		}
		return 0;
	}
	for(s=src;*s;s++,dst++)
	{
		*dst = *s;

		curr_swaps = word_swap_index[(U8)tolower(*s)];
		for(i=0;i<eaSize(&curr_swaps);i++)
		{
			swap = curr_swaps[i];

			if (strnicmp(s,swap->src,swap->src_len)==0)
			{
				if (swap->always_swap || checkEnds(s!=src,s,swap))
				{
					if (swap->swap_second && !swapped)
						continue;
					if (careful)
					{
#if 0
						if (swap->src_len < 4 && strncmp(s,swap->src,swap->src_len)!=0)
						{
							printf("  %s\n",src);
							continue;
						}
#endif
					}
					strcpy_s(dst,swap->dst_len+1,swap->dst);
					dst += swap->dst_len-1;
					s += swap->src_len-1;
					modified = 1;
					swapped = 1;
					break;
				}
				else
				{
					if (maybe)
						*maybe = 1;
				}
			}
		}
	}
	*dst = 0;
	if (modified)
		return buf;
	else
		return 0;
}

void checkWord(char *fname,char *src)
{
	int			i;
	WordSwap	*swap;

	for(i=0;i<eaSize(&word_swaps);i++)
	{
		swap = word_swaps[i];

		if (strstri(src,swap->src) && strcmp(swap->src,"Electro")!=0 && strcmp(swap->src,"Eel")!=0 && strcmp(swap->src,"XM")!=0 && strcmp(swap->src,"MU")!=0 && strcmp(swap->src,"DeptH")!=0)
		{
			if (strstri(swap->src,"aim") && !strstr(src,"AIM"))
				continue;
			fprintf(maybe_swaps,"%s: %s (%s)\n",fname,src,swap->src);
		}
	}
}

int doFixWord(char *buf,char *new_str,int *init,char *fname,StashTable unique_swaps,int is_max,int careful)
{
	int		maybe;
	if (fixWord(buf,new_str,careful,&maybe))
	{
		if (!*init)
		{
			if (swap_file)
				fprintf(swap_file,"\nmodify \"%s\"\n",fname);
			*init = 1;
		}
		if (swap_file && stashAddPointer(unique_swaps,buf,strdup(new_str),0) && !is_max)
			fprintf(swap_file,"  %s %s\n",buf,new_str);
		if (maybe)
			checkWord(fname,new_str);
		return 1;
	}
	else if (maybe)
	{
		checkWord(fname,buf);
	}
	return 0;
}

int badTexName(char *name)
{
	if (!strEndsWith(name,".tga"))
		return 0;
	if (strstr(name,"/"))
		return 0;
	if (strstr(name,"\\"))
		return 0;
	return 1;
}

char	g_fname[1000];

int renameWordsInFile(char *fname)
{
	int		init=0,max_file=0,raw_bin=0,modified=0;
	int		fsize,utf_count=0;
	char	*d,*dst,*label=0,*s,*mem,*utf_label=0,new_str[1000];
	StashTable	unique_swaps = stashTableCreateWithStringKeys(100,  StashDeepCopyKeys );;

	if (isBinData(fname))
		return 0;

	if (strEndsWith(fname,".max"))
	{
		max_file = 1;
		if (!g_findSwaps)
			return 0;
	}
	mem = fileAlloc(fname,&fsize);
	if (!mem)
		return 0;
	if (strEndsWith(fname,".fev") || strEndsWith(fname,".fsb"))
	{
		raw_bin = 1;
	}
	strcpy(g_fname,fname);
	curr_swap = 0;
	stashFindPointer(swapcmd_hashes,fname,&curr_swap);
	d = dst = calloc(fsize * 2,1);
	for(s=mem;s-mem < fsize;s++,d++)
	{
		*d = *s;
		if (utf_label)
			utf_count++;
		if (isalnum(*s) || *s == '_' || *s == '-' || *s =='/' || *s ==':' || *s =='.' || (*s =='\\' && max_file))
		{
			if (!label)
				label = s;
			if (!utf_label)
				utf_label = s;
		}
		else
		{
			char	buf[2000];
			int		len;

			if (max_file && utf_label)
			{
				if (*s != 0 || (!(utf_count&1)))
				{
					int		j,chunk_size,chunk_diff;
					U8		*src_base,*dst_base;

					len = s-utf_label;
					src_base = utf_label - 4;
					dst_base = &d[-len] - 4;
					chunk_size = src_base[0] + (src_base[1] << 8);
					if (len > 8 && len < sizeof(buf))
					{
						chunk_diff = chunk_size - len;
						for(j=0;j<len;j+=2)
							buf[j/2] = utf_label[j];
						buf[len/2] = 0;

						if (!strstri(buf,"eee") && doFixWord(buf,new_str,&init,fname,unique_swaps,max_file,max_file || raw_bin))
						{
							for(j=0;j<(int)strlen(new_str)*2;j+=2)
							{
								d[j-len] = new_str[j/2];
								d[j-len+1] = 0;
							}
							d += strlen(new_str)*2 - len;
							*d = *s;
							chunk_size = (int)strlen(new_str)*2 + chunk_diff;
							dst_base[0] = chunk_size & 255;
							dst_base[1] = (chunk_size>>8) & 255;
						}
					}
					utf_label = 0;
					utf_count = 0;
					label = 0;
				}
			}

			else if (label)
			{
				len = s-label;
				if (raw_bin && s-mem >= 4 && len < sizeof(buf) && len > 5)
				{
					int		chunk_size;
					U8		*src_base,*dst_base;

					strncpy(buf,label,len);
					buf[len] = 0;
					src_base = label - 4;
					dst_base = &d[-len] - 4;
					chunk_size = src_base[0] + (src_base[1] << 8);

					if (len < sizeof(buf) && len > 4 && len+1 == chunk_size)
					{
						if (doFixWord(buf,new_str,&init,fname,unique_swaps,max_file,max_file || raw_bin))
						{
							strcpy_s(d-len,strlen(new_str)+1,new_str);
							d += strlen(new_str) - len;
							*d = *s;

							chunk_size = (int)strlen(new_str) + 1;
							dst_base[0] = chunk_size & 255;
							dst_base[1] = (chunk_size>>8) & 255;
						}
					}
				}
				else if (!raw_bin)
				{
					strncpy(buf,label,len);
					buf[len] = 0;
					if (len >= 3)
					{
						if (doFixWord(buf,new_str,&init,fname,unique_swaps,max_file,max_file || raw_bin))
						{
							strcpy_s(d-len,strlen(new_str)+1,new_str);
							d += strlen(new_str) - len;
							*d = *s;
						}
					}
				}
				label = 0;
			}
		}
	}
	if ((d-dst != fsize || memcmp(mem,dst,fsize)!=0))
	{
		modified = 1;
		if (!g_findSwaps)
		{
			FILE	*outf;

#if DRYRUN
			{
				char	fname2[MAX_PATH];

				sprintf(fname2,"c:/temp/swaps/%s",getFileName(fname));
				outf = fopen(fname2,"wb");
			}
#else
			outf = fopen(fname,"wb");
#endif
			if (outf)
			{
				fwrite(dst,d-dst,1,outf);
				fclose(outf);
			}
			else
				printf("failed to modify %s\n",fname);
		}
	}

	free(dst);
	free(mem);
	if (init && max_file)
	{
		StashTableIterator iter;
		StashElement pElem;
		char		*src_name,*dst_name;

		fprintf(maxcmd_file,"%s\n",backSlashes(fname));
		stashGetIterator(unique_swaps, &iter);
		while (stashGetNextElement(&iter, &pElem))
		{
			src_name = stashElementGetStringKey(pElem);
			dst_name = stashElementGetPointer(pElem);
			if (!badTexName(src_name))
				fprintf(maxcmd_file,"%s %s\n",backSlashes(src_name),backSlashes(dst_name));
		}
	}
	stashTableDestroy(unique_swaps);

	return modified;
}

void delFileReq(FILE *file,char *fname,char *ext)
{
	char	buf[MAX_PATH];

	strcpy(buf,fname);
	strcat(buf,ext);
	if (fileExists(buf))
		fprintf(file,"\ndelete \"%s\"\n",buf);
}

static FileScanAction addFileToList(char *dir,struct _finddata32_t* data,void *pUserData)
{
	char	fname[MAX_PATH];
	char	*s=0;
	char	buf[1000];
	int		modified;

	StashTable	list = *(StashTable *)pUserData;

	sprintf(fname,"%s/%s",dir,data->name);
	if (strstri(fname,"fright"))
		printf("");
	if (stopLooking(fname))
		return FSA_NO_EXPLORE_DIRECTORY;
	if (!(data->attrib & _A_SUBDIR))
	{
		if (isUseless(fname))
			return FSA_EXPLORE_DIRECTORY;
		if (strnicmp(data->name,"terrain_",8)==0 || stricmp(data->name,"terrain.tlayer")==0)
		{
			s = strrchr(fname,'/');
			s--;
			while(*s != '/')
				s--;
			s++;
		}
		else if (s = strstri(fname,".zone"))
		{
			while(*s != '/')
				s--;
			s++;
		}
		else
			s = data->name;

		modified = 0;
		if (!isBinData(fname))
		{
			// skip any files larger than 50 megs, because obviously something is wrong.  Probably a bin file we missed.
			if (data->size > 50*1024*1024)
			{
				printf("WARNING:  Skipping large file %s, is this really a text file?  Other files of this type could be affected\n",data->name);
			}
			else
			{
				modified = renameWordsInFile(fname);
			}
		}

		if (fixWord(fname,buf,0,0))
		{
			if (strEndsWith(fname,".lgeo") || strEndsWith(fname,".bgeo") || strEndsWith(fname,".modelnames")	// 	|| strEndsWith(fname,".fev") || strEndsWith(fname,".fsb")
			|| strEndsWith(fname,".modelheader")  || strEndsWith(fname,".materialdeps")
			|| strEndsWith(fname,".skel") || strEndsWith(fname,".timestamp") || strEndsWith(fname,".atrk") )
				delFileReq(swap_file,fname,"");
			else
				fprintf(swap_file,"\nrename \"%s\" \"%s\"\n",fname,buf);
		}
		else if (modified)
		{
			char	basename[MAX_PATH];

			if (strEndsWith(fname,".fdp"))
			{
				strcpy(basename,fname);

			}
			else if (strEndsWith(fname,".wrl"))
			{
				strcpy(basename,fname);
				s = strstri(basename,"/src/");
				if (s)
				{
					strcpy_s(s,7,"/data/");
					s = strstri(fname,"/src/") + 5;
					strcat(basename,s);
					s = strrchr(basename,'.');
					*s = 0;
					s = strstri(basename,"/animation_library");
					if (s)
					{
						char	skeldir[MAX_PATH];
						char	*s2;

						s += strlen("/animation_library");
						strcpy(skeldir,basename);
						s2 = strstri(skeldir,"/animation_library") + strlen("/animation_library");
						strcat(skeldir,"skeletons");
						strcat(skeldir,s);
						delFileReq(swap_file,basename,".skel");
						delFileReq(swap_file,basename,".timestamp");
					}
					s = strstri(basename,"/character_library");
					if (s)
					{
						char	*s2;

						s += strlen("/character_library");
						s2 = strrchr(basename,'/');
						strcpy_s(s,strlen(s2)+1,s2);					
					}
					delFileReq(swap_file,basename,".lgeo");
					delFileReq(swap_file,basename,".bgeo");
					delFileReq(swap_file,basename,".modelnames");
					delFileReq(swap_file,basename,".modelheader");
					delFileReq(swap_file,basename,".materialdeps");
				}
			}
		}
	}
	return FSA_EXPLORE_DIRECTORY;
}

static int cmpSrcLen(const WordSwap **a,const WordSwap **b)
{
	return (*b)->src_len - (*a)->src_len;
}

void loadSwaps()
{
	WordSwap	*swap;
	char		*mem,*s,*args[100];
	int			argc;

	mem = s = fileAlloc("c:/temp/swaps/swaps.txt",0);
	while(s)
	{
		argc = tokenize_line(s,args,&s);
		if (!argc)
			continue;
		swap = calloc(sizeof(*swap),1);
		if (argc >= 2)
		{
			if (stricmp(args[0],"SEARCHDIR")==0)
			{
				eaPush(&search_dirs,strdup(args[1]));
				continue;
			}
			else if (args[0][0] == '*')
			{
				swap->always_swap = 1;
				swap->src		= strdup(&args[0][1]);
			}
			else if (args[0][0] == '!')
			{
				swap->swap_second = 1;
				swap->src		= strdup(&args[0][1]);
			}
			else if (args[0][0] == '@')
			{
				swap->common_word = 1;
				swap->src		= strdup(&args[0][1]);
			}
			else if (args[0][0] == '^')
			{
				swap->case_sensitive = 1;
				swap->src		= strdup(&args[0][1]);
			}
			else
				swap->src		= strdup(args[0]);
			swap->dst		= strdup(args[1]);
			swap->src_len	= (int)strlen(swap->src);
			swap->dst_len	= (int)strlen(swap->dst);
			eaPush(&word_swaps,swap);
		}
	}
	if (word_swaps)
		qsort(word_swaps,eaSize(&word_swaps),sizeof(WordSwap **),cmpSrcLen);
	{
		int		i,j;

		for(i=0;i<256;i++)
		{
			for(j=0;j<eaSize(&word_swaps);j++)
			{
				swap = word_swaps[j];

				if (tolower(swap->src[0]) == i)
					eaPush(&word_swap_index[i],swap);
			}
		}
	}
}

AUTO_COMMAND;
void findSwaps(int on)
{
	StashTable	dst_list;
	int			i;

	loadSwaps();
	swap_file = fopen("c:/temp/swaps/swapcmds.txt","wt");
	maxcmd_file = fopen("c:/temp/swaps/maxcmds.txt","wb");
	maybe_swaps = fopen("c:/temp/swaps/maybe_swaps.txt","wb");

	dst_list = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys );
	g_findSwaps = 1;
#if DRYRUN
	fileScanDirRecurseEx(search_dir, addFileToList, &dst_list);
#else
	for(i=0;i<eaSize(&search_dirs);i++)
		fileScanDirRecurseEx(search_dirs[i], addFileToList, &dst_list);
#endif
	fclose(swap_file);
	fclose(maxcmd_file);
	fclose(maybe_swaps);

	exit(0);
}


static FileScanAction addFileToHash(char *dir,struct _finddata32_t* data,void *pUserData)
{
	char	fname[MAX_PATH];

	StashTable	list = *(StashTable *)pUserData;

	sprintf(fname,"%s/%s",dir,data->name);
	if (data->name[0] == '_')
		return FSA_NO_EXPLORE_DIRECTORY;
	if (!(data->attrib & _A_SUBDIR))
	{
		stashAddPointer(list, data->name, strdup(fname), 0);
	}
	return FSA_EXPLORE_DIRECTORY;
}

static FileScanAction checkForRemnants(char *dir,struct _finddata32_t* data,void *pUserData)
{
	char	*s,fname[MAX_PATH];

	StashTable	list = *(StashTable *)pUserData;

	sprintf(fname,"%s/%s",dir,data->name);
	if (data->name[0] == '_')
		return FSA_NO_EXPLORE_DIRECTORY;
	if (!(data->attrib & _A_SUBDIR))
	{
		if (strEndsWith(data->name,".lgeo"))
		{
			char	src_name[MAX_PATH];

			strcpy(src_name,fname);
			s = strstri(src_name,"/data/");
			if (s)
			{
				strcpy_s(s,6,"/src/");
				s = strstri(fname,"/data/") + 6;
				strcat(src_name,s);
				s = strrchr(src_name,'.');
				*s = 0;
				strcat(src_name,".wrl");
				if (!fileExists(src_name))
				{
					char	*dirname;

					if (!stashFindPointer(list,getFileName(src_name),&dirname) && !strstri(fname,"tree_library"))
					{
						char	basename[MAX_PATH];

						strcpy(basename,fname);
						s = strrchr(basename,'.');
						if (s)
							*s = 0;
						fprintf(swap_file,"\ndelete \"%s.lgeo\"\n",basename);
						fprintf(swap_file,"\ndelete \"%s.bgeo\"\n",basename);
						fprintf(swap_file,"\ndelete \"%s.modelnames\"\n",basename);
						fprintf(swap_file,"\ndelete \"%s.modelheader\"\n",basename);
						fprintf(swap_file,"\ndelete \"%s.materialdeps\"\n",basename);
					}
				}
			}
		}
	}
	return FSA_EXPLORE_DIRECTORY;
}


AUTO_COMMAND;
void findRemnants(int on)
{
	StashTable all_files = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys );

	swap_file = fopen("c:/temp/swaps/swapcmds.txt","wt");

	fileScanDirRecurseEx("c:/fightclub", addFileToHash, &all_files);
	fileScanDirRecurseEx("c:/fightclub", checkForRemnants, &all_files);
}

AUTO_COMMAND;
void doSwaps(int on)
{
	char	*mem,*s,*args[100];
	int		i,argc;
	SwapCmd	*cmd=0;

	swapcmd_hashes = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys );

	mem = s = fileAlloc("c:/temp/swaps/swapcmds.txt",0);
	while(s)
	{
		int			flags = 0;

		argc = tokenize_line(s,args,&s);
		if (!argc || args[0][0] == '#')
			continue;
		if (stricmp(args[0],"rename")==0)
			flags = SWAPCMD_RENAME;
		else if (stricmp(args[0],"modify")==0)
			flags = SWAPCMD_MODIFY;
		else if (stricmp(args[0],"delete")==0)
			flags = SWAPCMD_DELETE;
		if (flags)
		{
			cmd = calloc(sizeof(*cmd),1);
			if (stashAddPointer(swapcmd_hashes, args[1], cmd, 0))
			{
				cmd->src_fname = strdup(args[1]);
				eaPush(&swapcmd_list,cmd);
			}
			else
			{
				free(cmd);
				stashFindPointer(swapcmd_hashes,args[1],&cmd);
			}
			if (flags & SWAPCMD_RENAME)
				cmd->dst_fname = strdup(args[2]);
			cmd->flags |= flags;
		}
		else if (cmd)
		{
			if (!cmd->allowed_swaps)
				cmd->allowed_swaps = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys );
			stashAddPointer(cmd->allowed_swaps, args[0], args[1], 0);
		}
	}

	// check out files to change
	{
		char	**filenames=0;
		int		ret=0;

		for(i=0;i<eaSize(&swapcmd_list);i++)
		{
			cmd = swapcmd_list[i];
			eaPush(&filenames,cmd->src_fname);
		}
#if !DRYRUN
		ret = gimmeDLLDoOperations(filenames, GIMME_CHECKOUT, GIMME_QUIET);
#endif

		for(i=0;i<eaSize(&filenames);i++)
		{
			if (fileIsReadOnly(filenames[i]))
				printf("RO %s\n",filenames[i]);
		}
	}

	// do file modifications
	for(i=0;i<eaSize(&swapcmd_list);i++)
	{
		int		t=0;

		cmd = swapcmd_list[i];

		if (cmd->flags & SWAPCMD_MODIFY)
			renameWordsInFile(cmd->src_fname);
#if !DRYRUN
		if (cmd->flags & SWAPCMD_RENAME)
		{
			mkdirtree(cmd->dst_fname);
			t= rename(cmd->src_fname,cmd->dst_fname);
			if (t != 0)
				printf("failed to rename %s to %s\n",cmd->src_fname,cmd->dst_fname);
		}
		if (cmd->flags & SWAPCMD_DELETE)
		{
			t= unlink(cmd->src_fname);
			if (t != 0)
				printf("failed to delete \"%s\"\n",cmd->src_fname);
		}
#endif
	}
	exit(0);
}
