#include "query.h"
#include "earray.h"
#include "utils.h"
#include "estring.h"

static int opMatchesStat(StatOp *op,Stat *stat)
{
	if (!op || !stat)
		return 0;
	if (stricmp(op->label,stat->key)==0)
	{
		switch(op->op)
		{
			xcase '=':
				if (strstri(stat->str,op->str))
					return 1;
			xcase '>':
				if (stat->value >= op->value)
					return 1;
			xcase '<':
				if (stat->value <= op->value)
					return 1;
			xcase 0:
					return 1;
		}
	}
	return 0;
}

static int opsMatchesStat(StatOp **ops,Stat *stat)
{
	int		i;

	if (!ops || !stat)
		return 0;
	for(i=0;i<eaSize(&ops);i++)
	{
		if (opMatchesStat(ops[i],stat))
			return 1;
	}
	return 0;
}

static void queryClearResults(QueryState *query)
{
	int		i,j;

	eaClear(&query->matches);

	for(i=0;i<eaSize(&query->tables);i++)
	{
		for(j=0;j<eaSize(&query->tables[i]->rows);j++)
			eaDestroy(&query->tables[i]->rows[j]->cols);
		eaDestroyEx(&query->tables[i]->rows,0);
		query->tables[i]->rows = 0;
	}
}

static int queryMatch(char *title,StatOp **ops,Stat ***stats)
{
	int		i,j,title_len;

	if (title)
		title_len = (int)strlen(title);
	for(i=0;i<eaSize(&ops);i++)
	{
		if (title && strnicmp(ops[i]->label,title,title_len) != 0)
			continue;
		for(j=eaSize(stats)-1;j>=0;j--)
		{
			if (opMatchesStat(ops[i],(*stats)[j]))
			{
				break;
			}
		}
		if (j < 0)
			return 0;
	}
	return 1;
}

U32 getNextUid(Stat **stats,U32 uid)
{
	int		i;
	U32		t,lowest=~0;

	for(i=0;i<eaSize(&stats);i++)
	{
		t = stats[i]->uid;
		if (t < lowest && t > uid)
			lowest = t;
	}
	if (lowest == ~0)
		return 0;
	return lowest;
}

void getUidMatches(Stat **stats,U32 uid,Stat ***matches)
{
	int		i;

	for(i=0;i<eaSize(&stats);i++)
	{
		if (uid == stats[i]->uid)
			eaPush(matches,stats[i]);
	}
}

int queryGatherMatches(QueryState *query,Stat **stats)
{
	int		i;
	Stat	*stat;

	queryClearResults(query);


	{
		U32		uid=0;
		Stat	**matches=0;
		char	title[200];
		int		title_len;

		for(;;)
		{
			eaSetSize(&matches,0);
			uid = getNextUid(stats,uid);
			if (!uid)
				break;
			getUidMatches(stats,uid,&matches);
			statGetTitle(matches[0]->key,title);
			strcat(title,"_");
			title_len = (int)strlen(title);

			for(i=eaSize(&query->all_ops)-1;i>=0;i--)
			{
				if (strnicmp(title,query->all_ops[i]->label,title_len)==0)
					break;
			}
			if (i < 0)
				continue;
			for(i=eaSize(&matches)-1;i>=0;i--)
			{
				if (opsMatchesStat(query->all_ops,matches[i]))
					break;
			}
			if (i<0)
				continue;
			if (!queryMatch(title,query->required_ops,&matches))
				continue;
			if (query->exclude_ops && queryMatch(title,query->exclude_ops,&matches))
				continue;
			for(i=0;i<eaSize(&matches);i++)
				eaPush(&query->matches,matches[i]);
		}
		eaDestroy(&matches);
	}

	for(i=0;i<eaSize(&stats);i++)
	{
		stat = stats[i];
		if (stat->uid==0 && opsMatchesStat(query->all_ops,stat))
			eaPush(&query->matches,stat);
	}
	if (!eaSize(&query->matches))
		return 0;
	if (!queryMatch(0,query->required_ops,&query->matches))
		return 0;
	if (query->exclude_ops && queryMatch(0,query->exclude_ops,&query->matches))
		return 0;
	return 1;
}

static void tableInsertRow(Table *table,Stat *stat)
{
	int		i;
	Row		*row=0;
	char	title[100];

	statGetTitle(stat->key,title);
	for(i=0;i<eaSize(&table->rows);i++)
	{
		if (stat->uid == table->rows[i]->uid)
			return;
	}
	row = calloc(sizeof(*row),1);
	eaSetSize(&row->cols,eaSize(&table->col_names));
	eaPush(&table->rows,row);
	row->uid = stat->uid;
}

void queryFillTableHoles(QueryState *query,SentryClient *sentry)
{
	int		i,j,k,valid=0;
	Table	*table;
	char	curr_title[200];

	for(i=0;i<eaSize(&query->tables);i++)
	{
		table = query->tables[i];
		for(j=0;j<eaSize(&query->matches);j++)
		{
			if (stricmp(statGetTitle(query->matches[j]->key,curr_title),table->title)==0)
			{
				tableInsertRow(table,query->matches[j]);
				valid = 1;
			}
		}

		for(j=0;j<eaSize(&table->rows);j++)
		{
			for(k=0;k<eaSize(&table->col_names);k++)
			{
				table->rows[j]->cols[k] = statFind(sentry,table->col_names[k],table->rows[j]->uid);
			}
		}
	}
	for(i=0;i<eaSize(&query->tables);i++)
	{
		Stat	*stat;

		table = query->tables[i];
		if (eaSize(&table->rows))
			continue;
		for(k=0;k<eaSize(&table->col_names);k++)
		{
			stat = statFind(sentry,table->col_names[k],0);
			if (stat)
			{
				tableInsertRow(table,stat);
				if (table->rows)
					table->rows[0]->cols[k] = stat;
			}
		}
	}
}

static void getTables(Table ***tables,char **cols)
{
	Table	*table;
	int		i;
	char	curr[100];

	for(i=0;i<eaSize(&cols);)
	{
		int		span=1;
		char	**table_names=0;

		statGetTitle(cols[i],curr);
		eaPush(&table_names,cols[i]);
		for(++i;i<eaSize(&cols);i++,span++)
		{
			if (strnicmp(cols[i],curr,strlen(curr))!=0)
				break;
			eaPush(&table_names,cols[i]);
		}
		table = calloc(sizeof(Table),1);
		strcpy(table->title,curr);
		table->col_names = table_names;
		eaPush(tables,table);
	}
}

void queryInit(QueryState *query,char *query_str)
{
	int		i,j,count=0,expand_table=1,colnames_only=0;
	char	*args[100],*cmd;

	eaPushUnique(&query->cols,stat_labels[0]);	// always get machine name

	if (query_str)
	{
		char *str,*s,*d;

		str = malloc(strlen(query_str) * 2 + 100);
		for(s=query_str,d=str;*s;s++,d++)
		{
			if (*s == '<' || *s == '>' || *s == '=')
			{
				*d++ = ' ';
				*d = *s;
				*(++d) = ' ';
			}
			else
				*d = *s;
		}
		*d = 0;
		count = tokenize_line(str,args,0);
		query->query_str = str;
	}
	for(j=0;j<count;j++)
	{
		int		required=1,exclude=0;
		StatOp	*op;

		cmd = args[j];
		if (cmd[0]=='&')
		{
			required = 1;
			cmd++;
		}
		if (cmd[0]=='|')
		{
			required = 0;
			cmd++;
		}
		else if (cmd[0]=='-')
		{
			exclude = 1;
			cmd++;
		}
		else if (cmd[0]=='+')
		{
			cmd++;
		}
		else if (stricmp(cmd,"NoExpand")==0)
		{
			expand_table=0;
			continue;
		}
		else if (stricmp(cmd,"Explicit")==0)
		{
			expand_table=0;
			colnames_only=1;
			continue;
		}
		if (j+2 < count && (args[j+1][0] == '<' || args[j+1][0] == '>' || args[j+1][0] == '='))
		{
			op = malloc(sizeof(*op));
			op->op = args[j+1][0];
			op->label = statsGetLabel(cmd);
			op->str = args[j+2];
			op->value = atof(op->str);
			j+=2;
			if (strlen(op->str) > 1)
			{
				char	t,*c;
				F64		mpy=1,curr,step=1000;

				c = op->str+strlen(op->str)-1;
				t = tolower(c[0]);
				if (t=='b')
				{
					step = 1024;
					t = tolower(c[-1]);
				}
				curr = step;
				if (t=='k')
					mpy=curr;
				curr *= step;
				if (t=='m')
					mpy=curr;
				curr *= step;
				if (t=='g')
					mpy=curr;
				curr *= step;
				if (t=='t')
					mpy=curr;
				curr *= step;
				if (t=='p')
					mpy=curr;
				op->value *= mpy;
			}
			if (op->label)
			{
				if (required)
					eaPush(&query->required_ops,op);
				else if (exclude)
					eaPush(&query->exclude_ops,op);
				eaPush(&query->all_ops,op);
				if (!colnames_only)
					eaPushUnique(&query->cols,op->label);

				if (expand_table)
				{
					for(i=0;i<eaSize(&stat_labels);i++)
					{
						char	title[200],curr_title[200];

						statGetTitle(op->label,title);
						if (stricmp(statGetTitle(stat_labels[i],curr_title),title)==0)
						{
							eaPushUnique(&query->cols,stat_labels[i]);
						}
					}
				}
			}
		}
		else
		{
			for(i=0;i<eaSize(&stat_labels);i++)
			{
				if (strstri(stat_labels[i],cmd))
				{
					eaPushUnique(&query->cols,stat_labels[i]);
				}
			}
		}
	}
	if (query->cols)
		qsort(query->cols,eaSize(&query->cols),sizeof(char **),labelCmp);
	getTables(&query->tables,query->cols);
}

void queryFree(QueryState *query)
{
	int		i,j;
	Table **tables = query->tables;

	for(i=0;i<eaSize(&tables);i++)
	{
		for(j=0;j<eaSize(&tables[i]->rows);j++)
			eaDestroy(&tables[i]->rows[j]->cols);
		eaDestroyEx(&tables[i]->rows,0);
		eaDestroy(&tables[i]->col_names);
	}
	eaDestroyEx(&query->tables,0);

	eaDestroy(&query->cols);
	eaDestroy(&query->matches);
	eaDestroyEx(&query->all_ops,0);
	eaDestroy(&query->required_ops);
	eaDestroy(&query->exclude_ops);
	if (query->query_str)
		free(query->query_str);
}

