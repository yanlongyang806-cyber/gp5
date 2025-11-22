#pragma once

#include "sentrycomm.h"

typedef struct
{
	char	*label;
	char	op;
	char	*str;
	F64		value;
} StatOp;

typedef struct
{
	Stat	**cols;
	int		uid;
} Row;

typedef struct
{
	char	title[200];
	char	**col_names;
	Row		**rows;
} Table;

typedef struct
{
	char	*query_str;
	StatOp	**all_ops;
	StatOp	**required_ops;
	StatOp	**exclude_ops;
	Stat	**matches;
	Table	**tables;
	char	**cols;
} QueryState;

int queryGatherMatches(QueryState *query,Stat **stats);
void queryFillTableHoles(QueryState *query,SentryClient *sentry);
void queryInit(QueryState *query,char *query_str);
void queryFree(QueryState *query);
void printQueryResultRow(char **estr,Table **tables);
