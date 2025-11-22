#include "net/net.h"
#include "file.h"
#include "estring.h"
#include "earray.h"
#include "sentrycomm.h"
#include "utils.h"
#include "HttpLib.h"
#include "mathutil.h"
#include "query.h"
#include "graph.h"
#include "error.h"
#include "sentryalerts.h"

static void printAllCommands(char **estr)
{
	int		i;
	char	*s,curr[200],last[200];

	estrConcatf(estr,"<BR>To do queries\n");
	estrConcatf(estr,"<TABLE BORDER><CAPTION ALIGN=TOP>All Stat commands</CAPTION>\n");
	estrConcatf(estr,"<TR>\n");
	estrConcatf(estr,"<TH>Type</TH>\n");
	estrConcatf(estr,"</TR><TR>\n");

	for(i=0;i<eaSize(&stat_labels);i++)
	{
		statGetTitle(stat_labels[i],curr);
		if (stricmp(curr,last)!=0)
		{
			if (i)
				estrConcatf(estr,"</TR>\n<TR>");
			estrConcatf(estr,"<TD>%s<TD><TD>",curr);
		}
		s = strchr(stat_labels[i],'_');
		if (!s++)
			s = "";
		estrConcatf(estr,"%s<TD>",s);
		strcpy(last,curr);
	}
	estrConcatf(estr,"</TR></TABLE>\n");
}

void printQueryResultRow(char **estr,Table **tables)
{
	int		i,j,k,colheight=1;
	Table	*table;
	Stat	*stat;

	for(i=0;i<colheight;i++)
	{
		estrConcatf(estr,"<TR><tbody>\n");
		for(j=0;j<eaSize(&tables);j++)
		{
			table = tables[j];
			for(k=0;k<eaSize(&table->col_names);k++)
			{
				estrConcatf(estr,"<TD>");
				if (i < eaSize(&table->rows))
				{
					stat = table->rows[i]->cols[k];
					estrConcatf(estr,"%s\n",stat ? stat->str : "");
				}
			}
			colheight = MAX(colheight,eaSize(&table->rows));
		}
		estrConcatf(estr,"</TR>\n");
	}
}

static printQueryForm(char **estr,char *query)
{
	char	*s,*d=0,buf[2] = "x";

	estrPrintf(&d,"");
	for(s=query;*s;s++)
	{
		if (*s == '"')
			estrConcatf(&d,"&quot;");
		else
		{
			buf[0] = *s;
			estrAppend2(&d,buf);
		}
	}

	estrConcatf(estr,"<html><body bgcolor=#7f7f7f> <font color=#ffaa22>\n");
	estrConcatf(estr,"<form name=\"input\"\n");
	estrConcatf(estr,"method=\"get\">\n");
	estrConcatf(estr,"Command \n");
	estrConcatf(estr,"<input type=\"text\" name=\"substrings\" value=\"%s\" size=100>\n",d);
	estrConcatf(estr,"</form>\n\n");
	estrConcatf(estr,"<BR><BR>\n");
	estrConcatf(estr,"</body></html>\n");

	estrDestroy(&d);
}

void printTableHeader(char **estr,char **cols,Table **tables,char *query)
{
	int		i;
	char	*s;

	estrConcatf(estr,"<TABLE border=\"1\" frame=\"border\" rules=\"groups\"\n");
	//estrConcatf(estr,"<CAPTION>Matches for strings \"%s\"</CAPTION>\n",query);
	for(i=0;i<eaSize(&cols);i++)
	{
		estrConcatf(estr,"<COLGROUP>\n");
	}
	estrConcatf(estr,"<THEAD>\n");
	estrConcatf(estr,"<TR>\n");
	for(i=0;i<eaSize(&tables);i++)
	{
		Table	*table = tables[i];

		estrConcatf(estr,"<TH colspan=\"%d\">%s\n",eaSize(&table->col_names),table->title);
	}
	estrConcatf(estr,"<TBODY>\n");

	for(i=0;i<eaSize(&cols);i++)
	{
		s = strchr(cols[i],'_');
		if (s)
			s++;
		else
			s = cols[i];
		estrConcatf(estr,"<TH>%s\n",s);
	}
	estrConcatf(estr,"<TBODY>\n");
}

static void relayToSentry(char *str)
{
	char	*args[10],*machine;
	int		count;

	count = tokenize_line(str,args,0);
	if (count < 3)
		return;
	machine = args[1];
	if (stricmp(args[0],"kill")==0)
		sentrySendKill(machine,args[2]);
	else if (stricmp(args[0],"launch")==0)
		sentrySendLaunch(machine,args[2]);
}

static void printStats(NetLink *link,char *query_str)
{
	char		*estr=0;
	int			i;
	QueryState	query = {0};

	if (!query_str)
		query_str = "";
	queryInit(&query,query_str); 
	printQueryForm(&estr,query_str);

	printTableHeader(&estr,query.cols,query.tables,query_str);
	for(i=0;i<eaSize(&sentries);i++)
	{
		SentryClient	*sentry = sentries[i];

		if (!queryGatherMatches(&query,sentry->stats))
			continue;
		queryFillTableHoles(&query,sentry);
		printQueryResultRow(&estr,query.tables);
	}
	estrConcatf(&estr,"</TABLE>\n");

	printAllCommands(&estr);
	estrConcatf(&estr,"<form name=\"input\"\n");
	estrConcatf(&estr,"method=\"get\">\n");
	estrConcatf(&estr,"<input type=\"submit\" name=\"Alerts\" value=\"Show Alerts\">\n");
	estrConcatf(&estr,"</form>\n\n");
	httpSendStr(link,estr);
	estrDestroy(&estr);
	queryFree(&query);
}

void graphStats(NetLink *link,char *cmd)
{
	char			*estr=0,*orig_query,*args[10];
	int				count;
	SentryClient	*sentry;
	Stat			*stat;

	strdup_alloca(orig_query,cmd);
	printQueryForm(&estr,orig_query);
	count = tokenize_line(cmd,args,0);
	if (count < 3)
	{
		estrConcatf(&estr,"not enough commands\n");
		goto done;
	}
	sentry = sentryFindByName(args[1]);
	if (!sentry)
	{
		estrConcatf(&estr,"can't find sentry: %s\n",args[1]);
		goto done;
	}
	stat = statFind(sentry,args[2],0);
	if (!stat)
	{
		estrConcatf(&estr,"can't find stat: %s\n",args[2]);
		goto done;
	}
#if 0
	for(i=0;i<ARRAY_SIZE(stat->seconds);i++)
	{
		estrConcatf(&estr,"<br>%f",stat->seconds[i]);
	}
#endif
	estrConcatf(&estr,"%s for last 60 seconds<br>",stat->key);
	{
		int	idx,num_vals = 60;
		F64	*values;

		values = _alloca(num_vals * sizeof(F64));
		idx = sentry->tag % num_vals;
		CopyStructs(values,stat->seconds + idx,num_vals - idx);
		CopyStructs(values + num_vals - idx,stat->seconds,idx);
		graphValues(values,60);
	}
	estrConcatf(&estr,"<img src=\"test.jpg\" border=\"2\">\n");
done:
	printAllCommands(&estr);
	httpSendStr(link,estr);
	estrDestroy(&estr);
}

void httpMsg(Packet *pkt,int cmd,NetLink *link,void *client)
{
	char	*data,*url_esc,*args[100] = {0},*values[100] = {0};
	int		count;

	data = pktGetStringRaw(pkt);
	url_esc = urlFind(data,"GET");
	if (!url_esc)
		return;
	count = urlToArgs(url_esc,args,values,ARRAY_SIZE(args));

	if (stricmp(args[0],"/")==0)
	{
		char	*s = values[1];

		if (s && (strnicmp(s,"graph ",5)==0))
			graphStats(link,values[1]);
		else if (s && (strnicmp(s,"kill ",5)==0 || strnicmp(s,"launch ",7)==0))
			relayToSentry(values[1]);
		else if (args[1] && (strnicmp(args[1],"alerts",5)==0))
			alertShow(link,s);
		else
			printStats(link,values[1]);
	}
	else
	{
		httpSendFileNotFoundError(link,args[0]);
		printf("Unknown command: %s\n",args[0]);
#if 0
		char	*mem,buf[MAX_PATH];
		U32		len;

		sprintf(buf,"c:/temp%s",args[0]);
		mem = fileAlloc(buf,&len);
		if (mem)
			httpSendBytes(link,mem,len);
#endif
	}
}

void webConnect(NetLink *link,SentryClient *client)
{
}

void webListen(NetComm *comm,int port)
{
	commListen(comm,LINKTYPE_UNSPEC, LINK_HTTP,port,httpMsg,webConnect,0,0);
}
