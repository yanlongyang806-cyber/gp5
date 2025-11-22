#pragma once

typedef struct NetLink NetLink;

AUTO_STRUCT;
typedef struct SentryAlert
{
	char	*query;
	char	*email;
	U8		alertIfFalse;
	int		*trigger_list; NO_AST
	int		num_triggered;
} SentryAlert;

AUTO_STRUCT;
typedef struct AlertList
{
	SentryAlert	**alerts;
} AlertList;

extern AlertList *alert_list;

void alertLoad();
void alertCheck();
void alertShow(NetLink *link,char *cmdline);
