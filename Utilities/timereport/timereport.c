#include "utils.h"
#include "file.h"
#include "mathutil.h"
#include <stdio.h>

typedef struct
{
	int		locked;
	char	what[200];
	F32		days;
} Task;

typedef struct
{
	char	name[200];
	char	project[100];
	Task	tasks[100];
	int		num_tasks;
} Report;

char	curr_project[100] = "Engine";

Report	reports[100];
int		num_reports;

void findTaskTimes(char **args,int count,Report *report)
{
	int		i,split;
	char	buf[1000] = "",*end;
	F32		scale;

	for(i=0;i<count;i++)
	{
		scale = 1;
		split = 0;
		if (strEndsWith(args[i],","))
		{
			split = 1;
			args[i][strlen(args[i])-1] = 0;
		}
		else if (strEndsWith(args[i],"d)"))
		{
			char	*c = &args[i][strlen(args[i])-3];

			if (isdigit(args[i][strlen(args[i])-3]))
				split = 1;
			else if (i && isdigit(args[i-1][strlen(args[i])-1]))
				split = 1;
		}
		if (stricmp(args[i],"days")==0)
			split = 1;
		if (i==count-1)
		{
			split = 1;
		}

		if (split)
		{
			int		j;
			char	*s;
			F32		scale = 1;
			Task	*task = &report->tasks[report->num_tasks++];

			strcat(buf,args[i]);
			end = buf + strlen(buf) - 1;
			if (end[0] == ')')
			{
				s = strrchr(buf,'(');
				if (s)
					*s = 0;
				end--;
			}
			if (*end == 'd')
				*end-- = 0;
			if (*end == 'h')
			{
				*end-- = 0;
				scale = 1.f/8;
			}
			if (stricmp(end-3,"days")==0)
			{
				end -= 4;
				end[1] = 0;
			}
			if (stricmp(end-2,"day")==0)
			{
				end -= 3;
				end[1] = 0;
			}
			for(;end >= buf; end--)
			{
				if (!isspace(*end))
					break;
			}
			for(;end >= buf; end--)
			{
				if (!(isdigit(*end) || *end == '.'))
					break;
			}
			task->days = scale * atof(end+1);
			if (isdigit(buf[0]) && !task->days)
			{
				task->days = atof(buf);
				memmove(buf,buf+2,strlen(buf)-2);
			}
			end[1] = 0;
			for(j=(int)strlen(buf)-1;j>=0;j--)
			{
				if (!isspace(buf[j]))
					break;
			}
			buf[j+1] = 0;
			if (strlen(buf) <= 1)
				report->num_tasks--;
			strcpy(task->what,buf);
			buf[0] = 0;
		}
		else
		{
			strcat(buf,args[i]);
			strcat(buf," ");
		}
	}
	{
		F32		total = 0,extra;
		int		num_zeroes=0;

		for(i=0;i<report->num_tasks;i++)
		{
			total += report->tasks[i].days;
			if (!report->tasks[i].days)
				num_zeroes++;
		}
		if (num_zeroes && total < 5)
		{
			extra = (5 - total) / num_zeroes;
			for(i=0;i<report->num_tasks;i++)
			{
				if (!report->tasks[i].days)
					report->tasks[i].days = extra;
			}
		}
	}

	for(i=0;i<report->num_tasks;i++)
	{
		//printf("   %s (%.1f)\n",report->tasks[i].what,report->tasks[i].days);
	}
}

void addReport(Report *report)
{
	int		i,j;
	Report	*curr;

	for(i=0;i<num_reports;i++)
	{
		if (stricmp(reports[i].name,report->name)==0)
			break;
	}
	if (i >= num_reports)
		num_reports++;
	curr = &reports[i];
	strcpy(curr->name,report->name);
	if (!curr->project[0])
		strcpy(curr->project,curr_project);

	for(i=0;i<report->num_tasks;i++)
	{
		for(j=0;j<curr->num_tasks;j++)
		{
			if (stricmp(curr->tasks[j].what,report->tasks[i].what)==0)
				break;
		}
		if (j >= curr->num_tasks)
			curr->num_tasks++;
		strcpy(curr->tasks[j].what,report->tasks[i].what);
		curr->tasks[j].days += report->tasks[i].days;
	}
}

processFile(char *mem)
{
	char	*name=0,*s,*args[1000];
	int		count,idx,in_paren=0;

	for(s=mem;*s;s++)
	{
		if (*s == '(')
			in_paren = 1;
		if (*s == ')' || *s == '\n' || *s == '\r')
			in_paren = 0;
		if (in_paren && *s == ',')
			*s = '/';
	}

	for(s=mem;;s)
	{
		count=tokenize_line(s,args,&s);
		if (!s)
			break;
		if (!count)
			continue;
		if (count == 2 && stricmp(args[0],"joe")==0 && stricmp(args[1],"w")==0)
			count = 1;
		if (count == 1)
		{
			if (stricmp(args[0],"Cities")==0)
				strcpy(curr_project,"CoH");
		}
		if (count == 2)
		{
			if (stricmp(args[0],"Fight")==0 && stricmp(args[1],"Club")==0)
				strcpy(curr_project,"FC");
			if (stricmp(args[0],"Lost")==0 && stricmp(args[1],"Worlds")==0)
				strcpy(curr_project,"LW");
		}
		if (count == 1 && strlen(args[0]) >= 2)
		{
			name = args[0];
			if (strEndsWith(name,":"))
				name[strlen(name)-1] = 0;
		}
		if (name && count >= 2 && (stricmp(args[0],"last")==0 || stricmp(args[0],"last:")==0 || stricmp(args[0],"lastweek")==0))
		{
			//printf("name: %s\n",name);
			if (strnicmp(args[1],"week",4)==0)
				idx = 2;
			else
				idx = 1;
			{
				Report report = {0};

				strcpy(report.name,name);
				findTaskTimes(args+idx,count-idx,&report);
				addReport(&report);
			}
			name=0;
		}
	}
}

void cleanDays(Report *report,F32 days,F32 threshold)
{
	int		i,idx=0;
	Task	*task;
	F32		total=0,scale,biggest=-1;

	for(i=report->num_tasks-1;i>=0;i--)
	{
		task = &report->tasks[i];
		if (stricmp(task->what,"vacation")==0)
			task->locked = 1;
		if (task->days < threshold)
		{
			memcpy(task,task+1,(report->num_tasks-i-1) * sizeof(*task));
			report->num_tasks--;
		}
	}
	for(i=report->num_tasks-1;i>=0;i--)
		total += report->tasks[i].days;
	scale = days / total;

	total = 0;
	for(i=report->num_tasks-1;i>=0;i--)
	{
		int		ival;
		F32		val,frac;

		if (!report->tasks[i].locked)
		{
			val = report->tasks[i].days * scale;
			ival = val;
			frac = val - ival;
			if (frac < .33)
				frac = 0;
			else if (frac > .66)
				frac = 1;
			else
				frac = .5;
			val = ival + frac;
			report->tasks[i].days = val;

			if (val > biggest)
			{
				biggest = val;
				idx = i;
			}
		}
		total += report->tasks[i].days;
	}
	report->tasks[idx].days += days - total;
	for(total=0,i=0;i<report->num_tasks;i++)
	{
		total += report->tasks[i].days;
	}
}

void addHolidays(Report *report,char *name,F32 days)
{
	int		i;
	F32		diff;

	for(i=0;i<report->num_tasks;i++)
	{
		if (strstri(report->tasks[i].what,name))
		{
			strcpy(report->tasks[i].what,name);
			diff = report->tasks[i].days - days;
			report->tasks[i].days = days;
			report->tasks[i].locked = 1;
			return;
		}
	}
	memmove(report->tasks+1,report->tasks,report->num_tasks * sizeof(report->tasks[0]));
	strcpy(report->tasks[0].what,name);
	report->tasks[0].days = days;
	report->tasks[0].locked = 1;
	report->num_tasks++;
}

char *getEmployeeProject(char *name)
{
	char	*coh[] = { "aaron", "vince", "garth", "brett" };
	int		i;

	for(i=0;i<ARRAY_SIZE(coh);i++)
	{
		if (stricmp(coh[i],name)==0)
			return "CoH";
	}
	return 0;
}

char *getTaskProject(char *task)
{
	char	*coh[] = { " cov", " coh", " cox", "coh "};
	char	*sick[] = { "sick", "surgery"};
	char	*vacation[] = { "vacation", "ooo"};
	int		i;

	for(i=0;i<ARRAY_SIZE(coh);i++)
	{
		if (strstri(task,coh[i]))
			return "CoH";
	}
#if 0
	for(i=0;i<ARRAY_SIZE(sick);i++)
	{
		if (strstri(task,sick[i]))
			return "General";
	}
	for(i=0;i<ARRAY_SIZE(vacation);i++)
	{
		if (strstri(task,sick[i]))
			return "General";
	}
#endif
	return 0;
}

void main(int argc,char **argv)
{
	int		i,j,size;
	char	*mem,*holiday_name;
	F32		total_days,holiday_days;

	fileDisableAutoDataDir();
	if (argc < 3)
		printf("Usage: timereport <holiday name> <holiday days> <total days> <report 1> ... <report n>\n");
	holiday_name	= argv[1];
	holiday_days	= atof(argv[2]);
	total_days		= atof(argv[3]);
	for(i=4;i<argc;i++)
	{
		mem = fileAlloc(argv[i],&size);
		processFile(mem);
		free(mem);
	}
	if (holiday_days)
	{
		for(i=0;i<num_reports;i++)
			addHolidays(&reports[i],holiday_name,holiday_days);
	}
	for(i=0;i<num_reports;i++)
		cleanDays(&reports[i],total_days, .49);
	for(i=0;i<num_reports;i++)
	{
		Report	*report;
		Task	*task;

		report = &reports[i];

		printf("\n%s\n",report->name);
		for(j=0;j<report->num_tasks;j++)
		{
			char	*task_project,*project=0;

			task = &reports[i].tasks[j];
			task_project = getTaskProject(task->what);
			if (task_project)
				project = task_project;
			else
				project = report->project;
			printf("  %2.2f days\t%8.8s\t%.40s\n",task->days,project,task->what);
		}
	}
}
