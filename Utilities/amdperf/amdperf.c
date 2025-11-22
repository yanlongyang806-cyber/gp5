#include <stdio.h>
#include <string.h>
#include "utils.h"
#include "file.h"
#include "StashTable.h"
#include "gimmeDLLWrapper.h"
#include "wininclude.h"

typedef struct FuncInfo
{
	char	name[256];
	int		module;
	int		ticks;
	int		id;
} FuncInfo;

FuncInfo	*func_info,*mod_info;
int			func_count,func_max;
int			mod_count,mod_max;
StashTable	module_lookup;

void loadModules(char *fname)
{
	char		*mem,*s,*args[100];
	FuncInfo	*mod;
	int			count;

	module_lookup = stashTableCreateWithStringKeys(800, StashDeepCopyKeys);
	mem = fileAlloc(fname,0);
	for(s=mem;s;)
	{
		count = tokenize_line(s,args,&s);
		if (!count)
			continue;
		if (stricmp(args[0],"MODULE")==0)
		{
			mod = dynArrayAdd(mod_info,sizeof(mod_info[0]),mod_count,mod_max,1);
			strcpy(mod->name,args[1]);
			mod->id  = mod_count-1;
		}
		else
			stashAddInt(module_lookup,args[0],mod_count-1,1);
	}
	free(mem);
}

int simpleBlock(char *s)
{
	if (strrchr(s,'+'))
		return 1;
	return 0;
}

int funcCmp(const FuncInfo *a, const FuncInfo *b)
{
	return b->ticks - a->ticks;
}

int funcCmpName(const FuncInfo *a, const FuncInfo *b)
{
	return strcmp(a->name,b->name);
}

U32			total_ticks=0,mod_ticks=0,little_ticks=0;
F32			threshold = 0.5f;

void printFuncsMatching(int mod_id)
{
	int			i;
	FuncInfo	*fi;
	int		curr_ticks=0;

	for(i=0;i<func_count;i++)
	{
		F32		percent,curr_percent;

		fi = &func_info[i];
		percent = (F32)fi->ticks * 100 / total_ticks;
		if (percent >= threshold && mod_id == fi->module)
		{
			curr_ticks += fi->ticks;
			curr_percent = (F32)curr_ticks * 100 / total_ticks;
#if 1
			printf("   %5.2f    %5.2f %s\n",curr_percent,percent,fi->name);
#else
			printf("%s\n",fi->name);
#endif
		}
		else if (mod_id < 0 && percent < threshold)
			little_ticks += fi->ticks;
	}
}

int main(int argc,char **argv)
{
	char		*s,*mem,*args[100],*module_name="c:/src/utilities/amdperf/modules.txt",csv_fname[MAX_PATH];
	FuncInfo	*fi;
	int			mod_idx,i,count;

	EXCEPTION_HANDLER_BEGIN
	DO_AUTO_RUNS
	gimmeDLLDisable(1);

	if (argc < 2)
	{
		printf("Usage: amdperf <amd perf.csv> <threshold percent> <module name file>\n");
		printf("       default threshold percent is 0.5%%\n");
		printf("       default module file name is c:/src/utilities/amdperf/modules.txt\n");
		exit(0);
	}
	if (argc > 2)
		threshold = atof(argv[2]);
	if (argc > 3)
		module_name = argv[3];
	if (module_name)
		loadModules(module_name);
	makefullpath(argv[1],csv_fname);
	mem = fileAlloc(csv_fname,0);
	if (!mem)
	{
		printf("cant open %s\n",argv[1]);
		exit(0);
	}
	for(s=mem;*s;s++)
	{
		if (*s == ',')
			*s = ' ';
	}
	s = mem;
	tokenize_line(s,args,&s);
	i=0;
	while((count=tokenize_line(s,args,&s)))
	{
		char	*start = args[1];

		if (strnicmp(args[1],"ILT+",4)==0)
		{
			char	*end;

			start = strchr(args[1],'(');
			if (!start++)
				continue;
			if (simpleBlock(start))
				continue;
			end = strrchr(start,')');
			if (end)
				*end = 0;
		}
		if (simpleBlock(start))
			continue;
		if (start[0] == '_' && stashFindInt(module_lookup,start+1,&mod_idx))
			start++;
		fi = dynArrayAdd(func_info,sizeof(func_info[0]),func_count,func_max,1);
		strcpy(fi->name,start);
		if (count >= 5)
			fi->ticks = atoi(args[4]);
		else
			fi->ticks = atof(args[3]) * 100000;
		total_ticks += fi->ticks;
		fi->id = i++;
		if (module_name && stashFindInt(module_lookup,fi->name,&mod_idx))
		{
			mod_info[mod_idx].ticks += fi->ticks;
			fi->module = mod_idx;
			mod_ticks += fi->ticks;
		}
		else
			fi->module = -1;
	}
	qsort(func_info,func_count,sizeof(*func_info),funcCmpName);
	/*
	 * for some reason codeanalyst finds duplicates that start with underscores.
	 * merge them into one.
	 */
	for(i=0;i<func_count-1;i++)
	{
		if (stricmp(func_info[i].name,func_info[i+1].name)==0)
		{
			func_info[i].ticks += func_info[i+1].ticks;
			func_info[i+1].ticks = 0;
		}
	}
	qsort(func_info,func_count,sizeof(*func_info),funcCmp);

	qsort(mod_info,mod_count,sizeof(*mod_info),funcCmp);

	if (module_name)
	{
		int		curr_ticks=0;

		for(i=0;i<mod_count;i++)
		{
			FuncInfo	*mod;

			mod = &mod_info[i];
			curr_ticks += mod->ticks;
			printf(" %5.2f     %5.2f %s\n",(F32)curr_ticks * 100 / total_ticks,(F32)mod->ticks * 100 / total_ticks,mod->name);
		}
	
		printf("\nPer function details:\n");
		curr_ticks=0;
		for(i=0;i<mod_count;i++)
		{
			FuncInfo	*mod;

			mod = &mod_info[i];
			curr_ticks += mod->ticks;
			printf(" %5.2f %s\n",(F32)mod->ticks * 100 / total_ticks,mod->name);
			printFuncsMatching(mod->id);
		}
		printf(" %4.1f non module\n",(F32)(total_ticks - mod_ticks)* 100 / total_ticks);
		printFuncsMatching(-1);
		printf(" %4.1f below threshold\n\n",(F32)(little_ticks)* 100 / total_ticks);
	}
	printf("");

	EXCEPTION_HANDLER_END
}
