#include "sock.h"
#include "net.h"
#include "timing.h"
#include "earray.h"
#include "estring.h"
#include "file.h"
#include "utils.h"
#include "error.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "timing.h"
#include "cmdparse.h"
#include "MemoryMonitor.h"
#include "gimmeDLLWrapper.h"
#include <sys/stat.h>
#include "scrubutils.h"
#include "renamer.h"
#include "StashTable.h"

StashTable	src_list,dst_list,dup_list,file_list,moved_list,ext_restricted,pair_accepted;
int			g_print,g_findrefs,g_filecount;

FILE		*cmd_file;
char		cmd_name[MAX_PATH];



char *findFileIfMoved(char *fname)
{
	char	*new_fname;

	if (fileExists(fname))
		return fname;

	if (stashFindPointer(moved_list,getFileName(fname),&new_fname))
		return new_fname;
	//printf("can't find %s\n",fname);
	return fname;
}



void setCmdFile(char *fname,int refcount,int size_diff)
{
	char	buf[MAX_PATH],real_cmd_name[MAX_PATH],*s;
	int		count=0,max_count=4;

	strcpy(buf,fname);
	if (strstri(fname,"texture_library"))
		max_count++;
	if (strstri(fname,"object_library"))
		max_count++;
	if (strstri(fname,"/defs/"))
		max_count++;
	for(s=buf;*s && count < max_count;s++)
	{
		if (*s == '/')
			count++;
	}
	if (count == max_count)
		s--;
	*s = 0;
#if 0
	if (refcount)
		strcat(buf,"_ref");
	else
		strcat(buf,"_unref");
#endif
	if (1)// || stricmp(cmd_name,buf)!=0)
	{
		strcpy(cmd_name,buf);
		if (cmd_file)
			fclose(cmd_file);
		for(s=buf;*s;s++)
		{
			if (*s == '/')
				*s = '_';
		}
		if (size_diff > 0)
			sprintf(real_cmd_name,"c:/temp/diffs/diffsize/%s.txt",buf+3);
		else if (size_diff < 0)
			sprintf(real_cmd_name,"c:/temp/diffs/samesize/%s.txt",buf+3);
		else
			sprintf(real_cmd_name,"c:/temp/diffs/both/%s.txt",buf+3);
		cmd_file = fopen(real_cmd_name,"a+b");
	}
}

static FileScanAction addFileForMoveCheck(char *dir,struct _finddata32_t* data,void *pUserData)
{
	char	fname[MAX_PATH];
	char	*s=0;

	StashTable	list = *(StashTable *)pUserData;

	if (data->name[0] == '_')
		return FSA_NO_EXPLORE_DIRECTORY;
	if (stricmp(data->name,"MasterBackup")==0)
		return FSA_NO_EXPLORE_DIRECTORY;
	if (!(data->attrib & _A_SUBDIR))
	{
		sprintf(fname,"%s/%s",dir,data->name);
		stashAddPointer(list, data->name, strdup(fname), 0);
	}
	return FSA_EXPLORE_DIRECTORY;
}

int total_unused;
static	char **co_filenames=0;

char *pathRename(char *src,char *dst,char *search,char *replace)
{
	char	*s;

	strcpy_s(dst,MAX_PATH,src);
	s = strstr(dst,search);
	if (!s)
		return 0;
	*s = 0;
	strcat_s(dst,MAX_PATH,replace);
	s = strstr(src,search);
	s += strlen(search);
	strcat_s(dst,MAX_PATH,s);
	return dst;
}

void addCheckoutFile(char *fname)
{
	char	src_name[MAX_PATH];

	pathRename(fname,src_name,"/data/","/src/");
	changeFileExt(src_name,".tga",src_name);

	eaPush(&co_filenames,strdup(fname));
	if (fileExists(src_name))
		eaPush(&co_filenames,strdup(src_name));
	else
		printf("");
}

static FileScanAction addFileToList(char *dir,struct _finddata32_t* data,void *pUserData)
{
	char	fname[MAX_PATH];
	char	*s=0;
	FInfo	*fi;

	StashTable	list = *(StashTable *)pUserData;

	sprintf(fname,"%s/%s",dir,data->name);
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
		if (g_print)
		{
			char	buf[MAX_PATH];
			FInfo	*fdup,*f2;
			int		i;

			if (stashFindPointer(src_list,s,&f2))
			{
				strcpy(buf,s);
				s = strrchr(buf,'.');
				if (s)
					*s = 0;
				if (stashFindPointer(dup_list,buf,&fdup))
				{
					char	*src_fname = "";

					for(i=0;i<eaSize(&fdup->refs);i++)
					{
						if (strEndsWith(fdup->refs[i],".costume"))
							return FSA_EXPLORE_DIRECTORY;
						if (strstri(fdup->refs[i],"Definitions"))
							return FSA_EXPLORE_DIRECTORY;
						if ((strstri(fdup->refs[i],"Signature_Heroes") || strstri(fdup->refs[i],"Signature_Villains") || strstri(fdup->refs[i],"Civilians")
							|| strstri(fdup->refs[i],"Animals") || strstri(fdup->refs[i],"Test_Costumes") || strstri(fdup->refs[i],"Avatars") || strstri(fdup->refs[i],"templates/character")))
						{
							if (!strstri(fdup->refs[i],"old"))
								return FSA_EXPLORE_DIRECTORY;
							eaPush(&co_filenames,strdup(fdup->refs[i]));
						}
						
					}
					addCheckoutFile(fdup->fname);

					total_unused += fdup->size;
					for(i=0;i<eaSize(&fdup->src_names);i++)
					{
						if (stricmp(fdup->dst_names[i],fname)==0)
							src_fname = fdup->src_names[i];
					}

					setCmdFile(fname,eaSize(&fdup->refs),fdup->f2->size != fdup->size ? 1 : -1);
					fprintf(cmd_file,"%s\n",getFileName(fname));
					fprintf(cmd_file,"%s   %d\n     %s   %d\n",fname,fdup->size,src_fname,fdup->f2->size);

					setCmdFile(fname,eaSize(&fdup->refs),0);
					if (1)
					{
						fdup->printed = 1;
						fprintf(cmd_file,"%s\n",getFileName(fname));
						fprintf(cmd_file,"%s   %d\n     %s   %d\n",fname,fdup->size,src_fname,fdup->f2->size);
						if (eaSize(&fdup->refs))
						{
							fprintf(cmd_file,"Refs:\n");
							for(i=0;i<eaSize(&fdup->refs);i++)
								fprintf(cmd_file,"  %s\n",fdup->refs[i]);
						}
					}
					fprintf(cmd_file,"keep\n\n");
				}
			}
			return FSA_EXPLORE_DIRECTORY;
		}
		fi = calloc(sizeof(FInfo),1);
		fi->size = data->size;
		fi->fname = strdup(fname);
		if (!src_list && !stashAddPointer(list, s, fi, 0))
		{
			FInfo	*f2;

			stashFindPointer(list,s,&f2);
			//printf("%s\n%s\n\n",f2->fname,s2);
		}
		if (src_list)
		{
			FInfo	*f2;

			if (stashFindPointer(src_list,s,&f2))
			{
				FInfo	*fdup;
				char	buf[MAX_PATH];

				strcpy(buf,s);
				s = strrchr(buf,'.');
				if (s)
					*s = 0;
				s=buf;

				if (!stashFindPointer(dup_list,s,&fdup))
				{
					fdup = calloc(sizeof(FInfo),1);
					*fdup = *fi;
					fdup->new_name = fdup->name = strdup(s);
					fdup->f2 = f2;
					stashAddPointer(dup_list, fdup->name, fdup, 0);
				}
				eaPush(&fdup->src_names,f2->fname);
				eaPush(&fdup->dst_names,fi->fname);
			}
		}
	}
	return FSA_EXPLORE_DIRECTORY;
}

typedef FileScanAction (*FileScanProcessor)(char* dir, struct _finddata32_t* data, void *pUserData);

StashTable buildList(char *dir)
{
	StashTable	list;

	list = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys );
	fileScanDirRecurseEx(dir, addFileToList, &list);

	return list;
}

int doAddRef(FInfo *fi,char *fname)
{
	int		i;

	if (!g_findrefs)
		return 0;
	if (stricmp(fi->name,"geometry")==0)
		return 1;
	if (stricmp(fi->name,"stance")==0)
		return 1;
	if (stricmp(fi->name,"default")==0)
		return 1;
	if (stricmp(fi->name,"powers")==0)
		return 1;
	if (stricmp(fi->name,"voice")==0)
		return 1;
	if (stricmp(fi->name,"something")==0)
		return 1;
	//printf("ref: %s   %s\n",fi->name,fname);
	for(i=0;i<eaSize(&fi->refs);i++)
	{
		if (stricmp(fi->refs[i],fname)==0)
			return 1;
	}
	eaPush(&fi->refs,strdup(fname));
	return 1;
}

char *getExt(char *fname)
{
	char	*s = strrchr(fname,'.');
	if (!s)
		s = fname;
	return s;
}

int refAllowed(char *src,char *ref)
{
	char	*src_ext,*ref_ext,both[2000];
	void	*t;

	src_ext = getExt(src);
	ref_ext = getExt(ref);

	if (stashFindPointer(ext_restricted,src_ext,&t))
	{
		sprintf(both,"%s%s",src_ext,ref_ext);
		return stashFindPointer(pair_accepted,both,&t);
	}
	return 1;
}

void renameObjsInFile(char *fname,StashTable list)
{
	FInfo	*fi;
	int		asked=0;

	if (!isBinData(fname) && !strEndsWith(fname,".max"))
	{
		int		fsize,utf_count=0;
		char	*d,*dst,*label=0,*s,*mem = fileAlloc(fname,&fsize),*utf_label=0;

		if (!mem)
			return;
		//printf("%s\n",fname);
		d = dst = malloc(fsize * 2);
		for(s=mem;s-mem < fsize;s++,d++)
		{
			*d = *s;
#if 0
			if ((*s & 0x80))
			{
				if (!((U8)*s == 0xe2 || (U8)*s == 0x80 || (U8)*s == 0xa6 || (U8)*s == 0x99|| (U8)*s == 0x94))
					;//printf("bin file %s\n",fname);
			}
#endif
			if (utf_label)
				utf_count++;
			if (isalnum(*s) || *s == '_')// || *s =='\\' || *s ==':' || *s =='.')
			{
				if (!label)
					label = s;
				if (!utf_label)
					utf_label = s;
			}
			else
			{
				char	buf[1000];
				int		len;

				if (0 && utf_label)
				{
					if (*s != 0 || (!(utf_count&1)))
					{
						int		j;

						len = s-utf_label;
						if (len > 8)
						{
							for(j=0;j<len;j+=2)
								buf[j/2] = utf_label[j];
							buf[len/2] = 0;
							label = 0;
							if (stashFindPointer(list,buf,&fi))
							{
								if (!doAddRef(fi,fname))
								{
									for(j=0;j<len;j+=2)
										d[j-len] = fi->new_name[j/2];
								}
								//printf("%s->%s\n%s\n\n",fi->name,fi->new_name,fname);
							}
						}
						utf_label = 0;
						utf_count = 0;
					}
				}
				if (label)
				{
					len = s-label;
					strncpy(buf,label,len);
					buf[len] = 0;
					label = 0;
					if (len > 4)
					{
						char	*skipx = buf;

						if (buf[0] == 'x' && buf[1] == '_')
							skipx += 2;
						if (stashFindPointer(list,skipx,&fi) && refAllowed(fi->fname,fname))
						{
							if (!doAddRef(fi,fname))
							{
								struct _stat64 sb = {0};

								_stat64(fname,&sb);
								if (!asked)
									printf("modifying %s",fname);
								asked = 1;
								printf(" %s->%s\n",fi->name,fi->new_name);
								strcpy_s(d-len,strlen(fi->new_name)+1,fi->new_name);
							}
							d += strlen(fi->new_name) - len;
							*d = *s;
						}
					}
				}
			}
		}
		if (!g_findrefs && (d-dst != fsize || memcmp(mem,dst,fsize)!=0))
		{
			FILE	*outf;

#if SINGLE_FILE_CHECKOUT
			checkoutFile(fname);
#endif
			outf = fopen(fname,"wb");
			if (outf)
			{
				fwrite(dst,d-dst,1,outf);
				fclose(outf);
			}
		}
		free(dst);
		free(mem);
	}
}

static FileScanAction renameObjects(char *dir,struct _finddata32_t* data,void *pUserData)
{
	char	fname[MAX_PATH];
	StashTable	list = *(StashTable *)pUserData;

	sprintf(fname,"%s/%s",dir,data->name);
	if (stopLooking(fname))
		return FSA_NO_EXPLORE_DIRECTORY;
	if (isUseless(fname) || isBinData(fname))
		return FSA_EXPLORE_DIRECTORY;
	g_filecount++;
	if (g_findrefs && !(g_filecount %100))
		printf("file %d  %s\n",g_filecount,fname);
	renameObjsInFile(fname,list);
	return FSA_EXPLORE_DIRECTORY;
}

static void loadRefFules()
{
	int		i,argc;
	char	*args[100],buf[1000],*mem,*s;

	mem = s = fileAlloc("c:/temp/diffs/_refrules.txt",0);
	ext_restricted = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys );
	pair_accepted = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys );
	while(s)
	{
		argc = tokenize_line(s,args,&s);
		if (!argc)
			continue;
		if (strEndsWith(args[0],":"))
			args[0][strlen(args[0])-1] = 0;
		stashAddPointer(ext_restricted, args[0], strdup(args[0]), 0);
		for(i=1;i<argc;i++)
		{
			sprintf(buf,"%s%s",args[0],args[i]);
			stashAddPointer(pair_accepted, buf, strdup(buf), 0);
		}
	}
}

static FileScanAction loadFixFile(char *dir,struct _finddata32_t* data,void *pUserData)
{
	char	*newname,*s,*s2,*end,*mem,fname[MAX_PATH];
	FInfo	*fi;
	StashTable	list = *(StashTable *)pUserData;
	int		ignore,i,j,len,line=0;
	FILE	*file;
	FInfo	**names=0;
	int		print_cmds=0;
	char	**ext_pairs=0;

	ignores = 0;
	sprintf(fname,"%s/%s",dir,data->name);

	if (strEndsWith(fname,"_refrules"))
		return FSA_EXPLORE_DIRECTORY;
	file = fopen(fname,"rt");
	fseek(file,0,2);
	len = ftell(file);
	mem = calloc(len,1);
	fseek(file,0,0);
	fread(mem,len,1,file);
	fclose(file);
	s = mem;

	for(;;)
	{
		for(;;)
		{
			if (strnicmp(s,"#ignore ",8)==0)
			{
				char	*src,*dst;
				FInfo	*ig = calloc(sizeof(*ig),1);

				ig->fname = calloc(100,1);

				for(src=s+8,dst=ig->fname;*src && *src != '\n';src++,dst++)
					*dst = *src;
				eaPush(&ignores,ig);
			}

			if (*s == '\n')
			{
				line++;
				s++;
			}
			else if (*s == '#')
				s = strchr(s,'\n');
			else
				break;
		}
		if (!*s)
			break;
		end = strchr(s,'\n');
		if (!end)
			break;
		fi = calloc(sizeof(FInfo),1);
		*end = 0;
		fi->name = strdup(s);
		s = end+1;
		line++;

		end = strchr(s,'\n');
		if (!end)
			break;
		*end = 0;
		s2 = strstri(s,fi->name);
		if (!s2)
			break;
		s2 += strlen(fi->name);
		*s2 = 0;
		fi->fname = strdup(findFileIfMoved(s));
		s = end+1;
		line++;

		for(;;)
		{
			end = strchr(s,'\n');
			if (*s == '\n' || *s == '#' || *s == ' ' || strnicmp(s,"refs:",5)==0)
			{
				line++;
				if (!end)
					goto done;
				if (s[0] == ' ' && s[3] == ':')
				{
					FInfo	*ref = calloc(sizeof(*ref),1);
					char	*src_ext,*ref_ext,both[2000],temp[2000];

					*end = 0;
					ref->fname = strdup(findFileIfMoved(s+2));
					ignore = 0;
					for(j=0;j<eaSize(&ignores);j++)
					{
						if (strEndsWith(ref->fname,ignores[j]->fname))
							ignore = 1;
					}

					src_ext = getExt(fi->fname);
					ref_ext = getExt(ref->fname);
					sprintf(both,"%s%s",src_ext,ref_ext);
					if (!refAllowed(fi->fname,ref->fname))
						ignore = 1;
					if (!ignore)
					{
						sprintf(temp,"src:%s",src_ext);
						sprintf(both,"%-15.15s ref:%s",temp,ref_ext);
						for(i=0;i<eaSize(&ext_pairs);i++)
						{
							if (stricmp(ext_pairs[i],both)==0)
								break;
						}
						if (i >= eaSize(&ext_pairs))
							eaPush(&ext_pairs,strdup(both));
						eaPush(&names,ref);
					}
				}
				s = end + 1;
			}
			else
				break;
		}

		*end = 0;
		if (strnicmp(s,"keep",4)==0)
		{
		}
		else if (strnicmp(s,"renameto",8)==0)
		{
			char	newfname[MAX_PATH];

			newname = s + 9;
			strcpy(newfname,fi->fname);
			s2 = strstri(newfname,fi->name);
			if (!s2 || !s2[0])
			{
				printf("error in line %d bad command: %s\n\n",line,s);
				fileError(0,0,0);
				exit(0);
			}

			strcpy_s(s2,MAX_PATH - (s2 - newfname),newname);
			fi->new_name = strdup(newname);
			fi->new_fname = strdup(newfname);
			if (print_cmds)
			{
				printf(" obj rename %s -> %s\n",fi->name,fi->new_name);
				printf("file rename %s ->\n            %s\n\n",fi->fname,fi->new_fname);
			}

			for(i=0;i<eaSize(&names);i++)
			{
				if (stashAddPointer(file_list, names[i]->fname, names[i], 0))
					eaPush(&file_names,names[i]);
			}
		}
		else if (strnicmp(s,"replacewith",11)==0)
		{
			newname = s + 12;
			fi->new_name = strdup(newname);

			if (print_cmds)
			{
				printf(" obj rename %s -> %s\n",fi->name,fi->new_name);
				printf("file delete %s\n\n",fi->fname);
			}

			for(i=0;i<eaSize(&names);i++)
			{
				stashAddPointer(file_list, names[i]->fname, names[i], 0);
				eaPush(&file_names,names[i]);
			}
		}
		else if (strnicmp(s,"delete",6)==0)
		{
			fi->new_name = 0; //strdup("DELETED");
			if (print_cmds)
				printf("file delete %s\n\n",fi->fname);
		}
		else
		{
			printf("error in line %d bad command: %s\n\n",line,s);
			fileError(0,0,0);
			exit(0);
		}
		s = end+1;
		line++;

		if (fi->new_name)
		{
			char	buf[1000];

			strcpy(buf,fi->name);
			s2 = strrchr(fi->name,'.');
			if (s2)
				*s2 = 0;
			s2 = strrchr(fi->new_name,'.');
			if (s2)
				*s2 = 0;
			if (stashAddPointer(list, fi->name, fi, 0))
			{
#if SINGLE_FILE_CHECKOUT
				checkoutFile(fi->fname);
#endif
				eaPush(&def_names,fi);
			}
		}
	}

done:
	printf("\next pairs for %s\n",fname);
	for(i=0;i<eaSize(&ext_pairs);i++)
	{
		printf("%s\n",ext_pairs[i]);
	}
	printf("\n");
	free(mem);
	return FSA_EXPLORE_DIRECTORY;
}

AUTO_COMMAND;
void fixup(char *src_dir,char *dst_dir)
{
	StashTable	list = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys );
	int			i,t;

	moved_list = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys );
	fileScanDirRecurseEx(dst_dir, addFileForMoveCheck, &moved_list);

	file_list = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys );

	fileScanDirRecurseEx(src_dir, loadFixFile, &list);

	if (g_dryrun)
		return;
	#if !SINGLE_FILE_CHECKOUT
	{
		char **filenames=0;
		int		ret,count;
		StashTable	uniques = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys );

		for(i=0;i<eaSize(&def_names);i++)
		{
			if (stashAddPointer(uniques, def_names[i]->fname, def_names[i], 0))
				eaPush(&filenames,def_names[i]->fname);
		}
		for(i=0;i<eaSize(&file_names);i++)
		{
			if (stashAddPointer(uniques, file_names[i]->fname, file_names[i], 0))
				eaPush(&filenames,file_names[i]->fname);
			else
				printf("");
		}
		count = eaSize(&filenames);
		ret = gimmeDLLDoOperations(filenames, GIMME_CHECKOUT, GIMME_QUIET);
		if (!ret)
			fileError("checkout some files",0,0);
	}
	#endif

	for(i=0;i<eaSize(&file_names);i++)
	{
		printf("%s\n",file_names[i]->fname);
		renameObjsInFile(file_names[i]->fname,list);
	}

	for(i=0;i<eaSize(&def_names);i++)
	{
		FInfo	*fi = def_names[i];

		printf("%s\n",fi->fname);
		if (fi->new_fname)
		{
			char	ms_src[MAX_PATH],ms_dst[MAX_PATH];

			t= rename(fi->fname,fi->new_fname);

			if (strEndsWith(fi->fname,".atrk"))
			{
				changeFileExt_s(fi->fname,".timestamp",ms_src, MAX_PATH);
				changeFileExt_s(fi->new_fname,".timestamp",ms_dst, MAX_PATH);

				if (fileExists(ms_src))
				{
					checkoutFile(ms_src);
					t= rename(ms_src,ms_dst);
					if (t!= 0)
						fileError("rename",ms_src,ms_dst);
				}
			}

			// ms files
			changeFileExt_s(fi->fname,".ms",ms_src, MAX_PATH);
			changeFileExt_s(fi->new_fname,".ms",ms_dst, MAX_PATH);

			if (fileExists(ms_src))
			{
				checkoutFile(ms_src);
				t= rename(ms_src,ms_dst);
				if (t!= 0)
					fileError("rename",ms_src,ms_dst);
			}

			// ms files with the original file extension also
			sprintf(ms_src,"%s.ms",fi->fname);
			sprintf(ms_dst,"%s.ms",fi->new_fname);

			if (fileExists(ms_src))
			{
				checkoutFile(ms_src);
				t= rename(ms_src,ms_dst);
				if (t!= 0)
					fileError("rename",ms_src,ms_dst);
			}

		}
		else
		{
			t = unlink(fi->fname);
			if (t!= 0)
				fileError("delete",fi->fname,0);
		}
	}
}

AUTO_COMMAND;
void findMatches(char *src_dir,char *dst_dir)
{
	mkdirtree(strdup("c:/temp/diffs/samesize/"));
	mkdirtree(strdup("c:/temp/diffs/diffsize/"));
	mkdirtree(strdup("c:/temp/diffs/both/"));
	dup_list = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys );
	src_list = buildList(src_dir);
	dst_list = buildList(dst_dir);
	g_findrefs = 1;
	fileScanDirRecurseEx(dst_dir, renameObjects, &dup_list);


	g_print = 1;
	fileScanDirRecurseEx(dst_dir, addFileToList, &dst_list);
}

AUTO_COMMAND;
void dryRun(int on)
{
	g_dryrun = 1;
}

#if 0
AUTO_COMMAND;
void memtest(int size,int count)
{
	int		i;
	extern void mmpl();

	for(i=0;i<count;i++)
	{
		malloc(size);
	}
	mmpl();
}
#endif

#include "mathutil.h"
#include "logging.h"
AUTO_COMMAND;
void logtest(int x)
{
	char	buf[10000];
	int		i,size;
	extern void logReset();

	for(;;)
	{
		for(i=0;i<100;i++)
		{
			size = randInt(ARRAY_SIZE(buf)-1);

			memset(buf,'x',size);
			buf[size] = 0;
			log_printf(LOG_TEST,"%s",buf);
		}
		logReset();
	}
}

#undef fopen
#undef fgets
AUTO_COMMAND;
void loghack(int x)
{
	char	*str,buf[10000],*args[100];
	int		count;
	void	*file;
	StashTable	histo;

	histo = stashTableCreateWithStringKeys(100,  StashDeepCopyKeys );;

	file = fopen("c:/temp/x.txt","rb");
	for(;;)
	{
		if (!fgets(buf,sizeof(buf)-1,file))
			break;
		count = tokenize_line(buf,args,0);
		if (count > 5)
		{
			int num_seen = 1;
			str = args[5];
			
			if (strnicmp(str,"newTrans",7)==0)
				str = args[6];
			if (strnicmp(str,"transUpdateDB",13)==0 || strnicmp(str,"transReturn",11)==0)
				continue;
			stashFindInt(histo,str,&num_seen);
			stashAddInt(histo, str, num_seen+1, 1);
		}
	}

	{
		StashTableIterator iterator;
		StashElement element;

		stashGetIterator(histo, &iterator);

		while (stashGetNextElement(&iterator, &element))
		{
			int	num_seen = stashElementGetInt(element);
			char *key = stashElementGetStringKey(element);

			if (num_seen > 1000)
				printf("%d %s\n",num_seen,key);
		}
	}
}

typedef struct LogCounter
{
	int		set;
	int		add;
	int		del;
	char	*label;
} LogCounter;

static int logCmp(const LogCounter **pLogA, const LogCounter **pLogB)
{
	return (*pLogA)->set - (*pLogB)->set;
}

static LogCounter **log_counts=0;
static StashTable	table;
static int detail_level = 1;

static void countLog(char *label,int type)
{
	LogCounter	*lc;
	char		buf[1000];

	if (1)
	{
		char	*s,*end;

		strcpy(buf,label);
		for(s=buf;s;)
		{
			s = strchr(s,'[');
			if (s)
			{
				if (detail_level == 1)
				{
					*s = 0;
					break;
				}
				end = strchr(s+1,']');
				if (!end)
					return;
				memmove(s+1,end,strlen(end)+1);
				s++;
			}
		}
		label = buf;
	}
	
	if (!stashFindPointer(table,label,&lc))
	{
		
		lc = calloc(sizeof(*lc),1);
		lc->label = strdup(label);
		eaPush(&log_counts,lc);
		stashAddPointer(table, label, lc, 0);
	}
	if (type == 0)
		lc->set++;
	else if (type == 1)
		lc->add++;
	else if (type == -1)
		lc->del++;
}

void logHack()
{
	char		*s,*mem;
	char		*arg_list[1000],**args;
	int			argc;
	int			i,line_num=0;
	LogCounter	*lc;

	table = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys );

	s = mem = fileAlloc("c:/temp/db/dbm.log",0);
	while(s)
	{
		line_num++;
		argc = tokenize_line(s,arg_list,&s);
		if (!argc)
			continue;
		args = arg_list;
		if (stricmp(args[3],"dbUpdateContainer")==0)
		{
			args += 6;
		}

		if (stricmp(args[0],"set")==0 && argc > 3)
		{
			countLog(args[1],0);
		}
		else if (stricmp(args[0],"create")==0)
		{
			countLog(args[1],1);
		}
		else if (stricmp(args[0],"destroy")==0)
		{
			countLog(args[1],-1);
		}
		else if (stricmp(args[3],"dbUpdateContainerOwner")==0)
		{
			//printf("dbUpdateContainerOwner\n");
		}
		else if (stricmp(args[0],"$$dbUpdateContainerOwner")==0)
		{
			//printf("dbUpdateContainerOwner\n");
		}
		else
		{
			//printf("\n");
		}
//		stashAddPointer(ext_restricted, args[0], strdup(args[0]), 0);
	}
	free(mem);

	eaQSort(log_counts, logCmp);

	{
		int		total = 0;

		for(i=0;i<eaSize(&log_counts);i++)
		{
			lc = log_counts[i];
			if (lc->set > 10)
			{
				if (detail_level == 1)
					printf("%-7d +%-6d -%-6d %s\n",lc->set,lc->add,lc->del,lc->label);
				else
					printf("%-7d %s\n",lc->set,lc->label);
			}
			total += lc->set;
			total += lc->add;
			total += lc->del;
		}
		printf("total: %d\n",total);

		if (0 && detail_level)
		{
			for(i=0;i<eaSize(&log_counts);i++)
			{
				lc = log_counts[i];
				if (lc->add > 10)
				{
					printf("%-7d +%-6d -%-6d %s\n",lc->set,lc->add,lc->del,lc->label);
				}
			}
		}
	}
}

typedef struct TransCounter
{
	U8			completed;
	U8			server_type;
	double		total;
	double		blocked;
	int			blocked_count;
	int			total_count;
	char		*label;
	int			histo[20];
} TransCounter;

TransCounter **trans_counts;

static char *getCmd(char *str)
{
	char	*s;

	if (str[1] == '(')
		str+=2;
	if (s = strrchr(str,'['))
		*s = 0;
	if (s = strchr(str,'('))
		*s = 0;
	if (s = strrchr(str,')'))
		*s = 0;
	if (s = strrchr(str,'('))
		printf("");
	return str;
}

static int getMsecs(char *str)
{
	char	*s;

	s = str + strlen(str) - 1;
	while(isdigit(*s))
		s--;
	return atoi(s+1);
}

TransCounter *addTransCount(char *name)
{
	TransCounter	*tc;

	if (!stashFindPointer(table,name,&tc))
	{
		tc = calloc(sizeof(*tc),1);
		tc->label = strdup(name);
		eaPush(&trans_counts,tc);
		stashAddPointer(table, tc->label, tc, 0);
	}
	return tc;
}

void setHisto(TransCounter *tc,int dt)
{
	int	idx = log2(dt/1000.f);

	if (idx < 0)
		idx = 0;
	if (idx >= ARRAY_SIZE(tc->histo))
		idx = ARRAY_SIZE(tc->histo)-1;
	tc->histo[idx]++;
}

void getTimes(char *trans_name,int argc,char **args)
{
	int				i;
	int				msecs=0,last_msecs=0,dt;
	char			*cmd,*blocker,*began,*server;
	TransCounter	*tc;

	for(i=0;i<argc;i++)
	{
		cmd = args[i];
		if (strcmp(cmd,"REQUESTED")==0)
		{
			server = getCmd(args[i+2]);
			//printf("%s from %s\n",trans_name,server);
		}
		if (strcmp(cmd,"BLOCKED")==0)
		{
			msecs = getMsecs(args[i+2]);
			dt = msecs - last_msecs;
			blocker = getCmd(args[i+2]);
			//printf("%d blocked by %s\n",dt,blocker);

			if (dt < 0)
				continue;
			tc = addTransCount(blocker);
			tc->blocked += dt;
			tc->blocked_count++;

		}
		else if (strcmp(cmd,"STEP")==0)
		{
			if (args[i+1][1] == '(')
			{
				msecs = getMsecs(args[i+5]);
				dt = msecs - last_msecs;
				if (dt < 0)
					continue;

				began = getCmd(args[i+1]);
				server = getCmd(args[i+5]);

				tc = addTransCount(began);
				tc->completed = 1;
				tc->total += dt;
				tc->total_count++;
				setHisto(tc,dt);

				tc = addTransCount(server);
				tc->server_type = 1;
				tc->total += dt;
				tc->total_count++;
				setHisto(tc,dt);

				//printf("%d completed %s on %s\n",dt,began,server);
			}
		}
		last_msecs = msecs;
	}
	//printf("\n");
}

#if 0
		if (stricmp(args[i],"msecs:")==0)
		{
			s = args[i-1] + strlen(args[i-1]) - 1;
			while(isdigit(*s))
				s--;
			msecs = atoi(s+1);
			cmd = args[i+1];
#endif

static int transCmp(const TransCounter **pLogA, const TransCounter **pLogB)
{
	return (*pLogA)->total - (*pLogB)->total;
}

static int transCmpBlocked(const TransCounter **pLogA, const TransCounter **pLogB)
{
	return (*pLogA)->blocked - (*pLogB)->blocked;
}

void transHack()
{
	char		*s,*curr,*mem;
	char		*arg_list[1000],**args;
	int			argc;
	int			j,i,line_num=0;
	//LogCounter	*lc;

	table = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys );

	curr = mem = fileAlloc("c:/temp/trans/trans.log",0);
	while(curr)
	{
		line_num++;
		argc = tokenize_line(curr,arg_list,&curr);
		if (!argc)
			continue;
		args = arg_list;
		for(i=0;i<argc;i++)
		{
			if (strcmp(args[i],"Trans")==0)
			{
				s = strchr(args[i+1],'(');
				if (s)
					*s = 0;
				getTimes(args[i+1],argc-i-2,args+i+2);
				//printf("Trans: %s\n",args[i+1]);
			}
		}
	}

	eaQSort(trans_counts, transCmp);
	printf("time to complete operations by server type\n");
	for(i=0;i<eaSize(&trans_counts);i++)
	{
		TransCounter	*tc = trans_counts[i];

		if (tc->server_type)
		{
			printf("%8.1f %6.1f %s\n",((double)tc->total)/1000,tc->total/(tc->total_count*1000),tc->label);
			for(j=0;j<ARRAY_SIZE(tc->histo);j++)
			{
				if (tc->histo[j])
					printf("\t%5d < %dsec\n",tc->histo[j],1<<j);
			}
		}
	}
	printf("\n");

	printf("time to complete operations\n");
	for(i=0;i<eaSize(&trans_counts);i++)
	{
		TransCounter	*tc = trans_counts[i];

		if (tc->completed)
		{
			printf("%8.1f %6.1f %s\n",tc->total/1000,tc->total/(tc->total_count*1000),tc->label);
			for(j=ARRAY_SIZE(tc->histo)-1;j>=0;j--)
			{
				if (tc->histo[j])
					break;
			}

			if (j > 3)
			{
				for(j=0;j<ARRAY_SIZE(tc->histo);j++)
				{
					if (tc->histo[j])
						printf("\t%5d < %dsec\n",tc->histo[j],1<<j);
				}
			}
		}
	}
	printf("\n");

	eaQSort(trans_counts, transCmpBlocked);
	printf("time caused by other transactions blocking\n");
	for(i=0;i<eaSize(&trans_counts);i++)
	{
		TransCounter	*tc = trans_counts[i];

		if (tc->blocked)
			printf("%8.1f %6.1f %s\n",tc->blocked/1000,tc->blocked/(tc->blocked_count*1000),tc->label);
	}
}

int main(int argc,char **argv)
{
	int		timer = timerAlloc(), alert_timer = timerAlloc();
	extern void testCollCache();

	EXCEPTION_HANDLER_BEGIN
	DO_AUTO_RUNS

	//logHack();
	//transHack();
	//exit(0);

	loadRefFules();
	cmdParseCommandLine(argc,argv);
	if (argc <= 1)
	{
		char	buf[100];

		printf("Usage: scrubber [-findswaps] [-doswaps]\n");
		gets(buf);
		return 0;
	}

	{
		int		i,ret;

		ret = gimmeDLLDoOperations(co_filenames, GIMME_CHECKOUT, GIMME_QUIET);
		if (!ret)
			fileError("checkout some files",0,0);

		for(i=0;i<eaSize(&co_filenames);i++)
		{
			char	*s,buf[1000],rename_buf[MAX_PATH];

			strcpy(rename_buf,co_filenames[i]);
			s = strstr(rename_buf,"costumes/");
			strcpy_s(s,strlen("costumes/_old/")+1,"costumes/_old/");
			s = strstr(co_filenames[i],"costumes/");
			strcat(rename_buf,s+strlen("costumes/"));
			mkdirtree(rename_buf);
			sprintf(buf,"mv \"%s\" \"%s\"",co_filenames[i],rename_buf);
			if (!fileIsReadOnly(co_filenames[i]))
				system(buf);
			else
				printf("");
		}
	}
	EXCEPTION_HANDLER_END
	return 0;
}



