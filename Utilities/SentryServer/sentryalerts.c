#include "sentryalerts.h"
#include "autogen/sentryalerts_h_ast.c"
#include "fileutil.h"
#include "error.h"
#include "earray.h"
#include "sentrycomm.h"
#include "query.h"
#include "estring.h"
#include "HttpLib.h"
#include "stashtable.h"
#include "timing.h"
#include "Organization.h"

AlertList	*alert_list;

void sendEmail(char *from,char *to,char *pTitle,char *msg)
{
	FILE	*file;
	char	*pBigSystemString = NULL;
	char	*fname = "c:/sentryalert.txt";

	file = fopen(fname,"wb");
	if (file)
	{
		fprintf(file,"%s",msg);
		fclose(file);
		estrPrintf(&pBigSystemString,"bmail.exe -s universe." ORGANIZATION_DOMAIN " -t %s -f %s -a \"%s\" -m \"%s\" -c",to,from,pTitle, fname);
	}
	else
		estrPrintf(&pBigSystemString,"bmail.exe -s universe." ORGANIZATION_DOMAIN " -t %s -f %s -a \"%s\" -b \"%s\" -c",to,from,pTitle, msg);
	//printf("%s\n",pBigSystemString);
	system_detach(pBigSystemString, 0, true);
	estrDestroy(&pBigSystemString);
}

void alertLoad()
{
	char		curr_dir[MAX_PATH],fname[MAX_PATH];
	char		*estr=0;
	StashTable	triggers;
	int			i,j;
	SentryAlert	*alert;

	// store trigger states
	triggers = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys_NeverRelease );
	for(i=0;alert_list && i<eaSize(&alert_list->alerts);i++)
	{
		alert = alert_list->alerts[i];
		ea32SetSize(&alert->trigger_list,eaSize(&sentries));
		for(j=0;j<eaSize(&sentries);j++)
		{
			estrPrintf(&estr,"%s.%s.%d.%s",alert->query,alert->email,alert->alertIfFalse,sentries[j]->name);
			stashAddInt(triggers, estr, alert->trigger_list[j], 0);
		}
	}

	// reload
	fileGetcwd(curr_dir,sizeof(curr_dir));
	sprintf(fname,"%s/%s",curr_dir,"SentryAlerts.txt");
	alert_list = StructCreate(parse_AlertList);
	if (!ParserReadTextFile(fname, parse_AlertList, alert_list, 0))
	{
		if (fileExists(fname))
			FatalErrorf("failed to parse %s!",fname);
	}

	// restore trigger states
	for(i=0;i<eaSize(&alert_list->alerts);i++)
	{
		int		triggered;

		alert = alert_list->alerts[i];
		ea32SetSize(&alert->trigger_list,eaSize(&sentries));
		for(j=0;j<eaSize(&sentries);j++)
		{
			estrPrintf(&estr,"%s.%s.%d.%s",alert->query,alert->email,alert->alertIfFalse,sentries[j]->name);
			if (stashFindInt(triggers, estr, &triggered))
				alert->trigger_list[j] = triggered;
		}
	}
	stashTableDestroy(triggers);
	estrDestroy(&estr);

}

static void printRow(char **estr,Table **tables)
{
	int		i,j,k,colheight=1;
	Table	*table;
	Stat	*stat;

	for(i=0;i<colheight;i++)
	{
		estrConcatf(estr,"");
		for(j=0;j<eaSize(&tables);j++)
		{
			table = tables[j];
			for(k=0;k<eaSize(&table->col_names);k++)
			{
				estrConcatf(estr,"\t");
				if (i < eaSize(&table->rows))
				{
					stat = table->rows[i]->cols[k];
					estrConcatf(estr,"%s",stat ? stat->str : "");
				}
			}
			colheight = MAX(colheight,eaSize(&table->rows));
		}
		estrConcatf(estr,"\n");
	}
}

extern void printTableHeader(char **estr,char **cols,Table **tables,char *query);

static void alertTriggered(char *query_str,SentryAlert *alert)
{
	QueryState	query = {0};
	int			i,triggered;
	static char	*equery=0,*email=0,*subject=0;

	ea32SetSize(&alert->trigger_list,eaSize(&sentries));
	queryInit(&query,query_str);
	for(i=0;i<eaSize(&sentries);i++)
	{
		SentryClient	*sentry = sentries[i];

		if (!queryGatherMatches(&query,sentry->stats))
			triggered = 0;
		else
		{
			triggered = 1;
		}
		if (alert->alertIfFalse)
			triggered = !triggered;
		if (triggered != alert->trigger_list[i])
		{
			queryFillTableHoles(&query,sentry);
#if 0
			estrPrintf(&equery,"<html><body>");
			printTableHeader(&equery,query.cols,query.tables,query_str);
			printQueryResultRow(&equery,query.tables);
			estrPrintf(&equery,"</body></html>");
#else
			estrPrintf(&equery,"");
			printRow(&equery,query.tables);
#endif

			alert->trigger_list[i] = triggered;
			if (triggered)
			{
				estrPrintf(&subject,"Alert on %s: %s",sentry->name,alert->query);
				if (alert->alertIfFalse)
					estrPrintf(&email,"query \"%s\" Triggered beacuse query failed. (Alert if false.)",alert->query);
				else
					estrPrintf(&email,"Query \"%s\" Triggered.\nQuery result:\n%s\n",alert->query,equery);
				printf("%s: %s emailed to %s\n",timeGetLocalDateString(),subject,alert->email);
				sendEmail("sentry@" ORGANIZATION_DOMAIN,alert->email,subject,email);
			}
			else
			{
				estrPrintf(&subject,"AOK on %s %s",sentry->name,alert->query);
				estrPrintf(&email,"Query \"%s\" Is now AOK.\nQuery result:\n%s\n",alert->query,equery);

				printf("%s: %s emailed to %s\n",timeGetLocalDateString(),subject,alert->email);
				sendEmail("sentry@" ORGANIZATION_DOMAIN,alert->email,subject,email);
			}
		}
	}
	queryFree(&query);
}

void alertCheck()
{
	int		i;
	SentryAlert	*alert;

	for(i=0;i<eaSize(&alert_list->alerts);i++)
	{
		alert = alert_list->alerts[i];

		alertTriggered(alert->query,alert);
	}
}

void alertShow(NetLink *link,char *cmdline)
{
	WriteHTMLContext	html_context = {0};
	char				*estr=0;
	int					i,j;

	alertLoad();
	estrConcatf(&estr,"<html><body bgcolor=#7f7f7f> <font color=#ffaa22>\n");
	for(i=0;i<eaSize(&alert_list->alerts);i++)
	{
		alert_list->alerts[i]->num_triggered = 0;
		for(j=0;j<eaSize(&sentries);j++)
			alert_list->alerts[i]->num_triggered += alert_list->alerts[i]->trigger_list[j];
	}
	ParserWriteHTML(&estr, parse_AlertList, alert_list, &html_context);
	estrConcatf(&estr,"<form name=\"input\"\n");
	estrConcatf(&estr,"method=\"get\">\n");
	estrConcatf(&estr,"<input type=\"submit\" name=\"substrings\" value=\"back to main page\">\n");
	estrConcatf(&estr,"</form>\n\n");
	estrConcatf(&estr,"<BR><BR>\n");
	estrConcatf(&estr,"</body></html>\n");

	httpSendStr(link,estr);
	estrDestroy(&estr);
}
