#include "file.h"
#include "utils.h"
#include "stashtable.h"
#include "earray.h"

typedef struct
{
	char	*name;
	U32		size;
	U32		raw;
} FileDiff;

FileDiff	**diffs,**exts,**maps;
StashTable	ext_hashes;

U32 atoi_fixup(char *str)
{
	char	*s;
	F32		val = atof(str);

	s = str + strlen(str) - 1;
	if (*s == 'B')
	{
		if (s[-1] == 'K')
			val *= 1024;
		if (s[-1] == 'M')
			val *= 1024 * 1024;
	}
	return val;
}

int sizeSort(const FileDiff **a, const FileDiff **b)
{
	return (*a)->size - (*b)->size;
}

char *getMapName(char *name,char *mapname)
{
	char	*s;

	s = strstri(name,"/maps/");
	if (s)
	{
		s = strchr(s + strlen("/maps/"),'/');
#if 0
		if (s)
			s = strchr(s+1,'/');
#endif
		if (s++)
		{
			strcpy_s(mapname,MAX_PATH,s);
			s = strchr(mapname,'/');
			if (s)
				*s = 0;
			return mapname;
		}
	}
	return 0;
}

void printList(FileDiff	**list)
{
	int			i;
	FileDiff	*ext;
	F32			MBSIZE = 1024*1024,total_diff=0,total_raw=0;

	eaQSort(list, sizeSort);
	for(i=0;i<eaSize(&list);i++)
	{
		ext = list[i];
		printf(" %5.1f / %5.1f %s\n",ext->size/MBSIZE,ext->raw/MBSIZE,ext->name);
		total_diff += ext->size;
		total_raw += ext->raw;
	}
	printf(" ----------------------------------\n");
	printf(" %5.1f / %5.1f %s\n",total_diff/MBSIZE,total_raw/MBSIZE,"Total");
}

void addDiff(FileDiff ***listp,FileDiff *diff,char *name)
{
	FileDiff	*ext;

	if (!stashFindPointer(ext_hashes,name,&ext))
	{
		ext = calloc(sizeof(*ext),1);
		ext->name = strdup(name);
		stashAddPointer(ext_hashes,name,ext,0);
		eaPush(listp,ext);
	}
	ext->raw += diff->raw;
	ext->size += diff->size;
}

void analyzeDiffs(char *mem)
{
	char		*s,*args[100],buf[MAX_PATH*2],mapname[MAX_PATH];
	FileDiff	*diff;
	F32			MBSIZE = 1024*1024,total_diff=0,total_raw=0;
	int			argc;

	ext_hashes = stashTableCreateWithStringKeys(200,  StashDeepCopyKeys_NeverRelease );
	s = mem = strdup(mem);
	while(s)
	{
		char	*ext_name;
		int		skip = 0;

		if (*s == ' ')
			skip = 1;
		argc = tokenize_line(s,args,&s);
		if (argc < 4 || skip)
			continue;
		diff = malloc(sizeof(*diff));
		if (args[0][strlen(args[0])-1] == ':')
			args[0][strlen(args[0])-1] = 0;
		diff->name = strdup(args[0]);
		diff->size = atoi_fixup(args[1]);
		diff->raw = atoi_fixup(args[3]);
		eaPush(&diffs,diff);

		if (getMapName(diff->name,mapname))
			addDiff(&maps,diff,mapname);

		ext_name = FindExtensionFromFilename(diff->name);
		if (stricmp(ext_name,".mset")==0)
		{
			if (strstri(diff->name,"/ol/"))
				sprintf(buf,".mset_ObjLib");
			else if (strstri(diff->name,"/cl/"))
				sprintf(buf,".mset_CharLib");
			else
				sprintf(buf,".mset_Unknown");
			ext_name = buf;
		}
		if (stricmp(ext_name,".hogg")==0)
		{
			ext_name = strrchr(diff->name,'/');
			if (ext_name)
			{
				char	*s2,base[MAX_PATH*2];

				ext_name++;
				strcpy(base,ext_name);
				s2 = strchr(base,'_');
				if (s2)
				{
					*s2 = 0;
					s2 = strrchr(ext_name,'_');
					if (isdigit(s2[1]))
						s2 = strrchr(ext_name,'.');
					sprintf(buf,"%s%s",base,s2);
					ext_name = buf;
				}
			}
		}
		if (ext_name)
			addDiff(&exts,diff,ext_name);
	}
	printf(" Diffs by map type:\n");
	printList(maps);
	printf("\n Diffs by extension:\n");
	printList(exts);
	free(mem);
}

AUTO_COMMAND;
void diffalyzer(char *fname)
{
	char		*mem;

	mem = fileAlloc(fname,0);
	analyzeDiffs(mem);
	free(mem);
	exit(0);
}
