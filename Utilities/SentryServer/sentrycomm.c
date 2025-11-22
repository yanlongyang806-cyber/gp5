#include "net/net.h"
#include "earray.h"
#include "sentrycomm.h"
#include "sentry_comm.h"
#include "StashTable.h"
#include "assert.h"
#include "utils.h"
#include "zutils.h"
#include "file.h"
#include "sysutil.h"
#include "crypt.h"
#include "netpacketutil.h"
#include "timing.h"
#include "mathutil.h"
#include "HashFunctions.h"
#include "sock.h"
#include "logging.h"
#include "monitor.h"
#include "SentryPub_h_ast.h"
#include "CrypticPorts.h"
#include "SentryServer.h"
#include "Alerts.h"
#include "TimedCallback.h"
#include "StringCache.h"
#include "structNet.h"

SentryClient **sentries;
StashTable	stat_label_hashes;
static char *g_sentry_name;

AUTO_RUN;
void SentriesInit(void)
{
	eaIndexedEnable(&sentries, parse_SentryClient);
}

SentryClient *sentryFindByName(char *machine)
{
	U32				ip;
	int				i;
	SentryClient	*client;

	if (!isIp(machine))
	{
		return eaIndexedGetUsingString(&sentries, machine);
	}
	else
	{
		ip = inet_addr(machine);
		for(i=0;i<eaSize(&sentries);i++)
		{
			client = sentries[i];

			if (ip == linkGetSAddr(client->link))
				return client;
			if (ip == client->local_ip)
				return client;
			if (ip == client->public_ip)
				return client;
		}
	}
	return 0;
}

SentryClient *sentrySendKill(char *machine,char *process_name)
{
	SentryClient	*client;

	client = sentryFindByName(machine);

	if (client)
	{
		if (clientIsServerSideReadOnly(client->name))
		{
			printf("FAILED killing \"%s\" on %s because machine is server-side readonly\n",process_name,machine);
			filelog_printf("launchcmds.log","FAILED killing \"%s\" on %s because machine is server-side readonly\n",process_name,machine);
			return NULL;
		}

		if (client->read_only_local)
		{
			printf("FAILED killing \"%s\" on %s because machine is client-side readonly\n",process_name,machine);
			filelog_printf("launchcmds.log","FAILED killing \"%s\" on %s because machine is client-side readonly\n",process_name,machine);
			return NULL;
		}
	}


	if (client && client->link)
	{
		Packet			*relay = pktCreate(client->link,SENTRYSERVER_KILL);

		pktSendString(relay,process_name);
		pktSend(&relay);
	}
	return client;
}

SentryClient *sentrySendGetFileCRC(char *machine, int iRequestID, char *pFileName, int iMonitorLinkID)
{
	SentryClient	*client;

	client = sentryFindByName(machine);
	if (client && client->link)
	{
		Packet *relay = pktCreate(client->link, SENTRYSERVER_GETFILECRC);
		pktSendBits(relay, 32, iRequestID);
		pktSendString(relay, pFileName);
		pktSendBits(relay, 32, iMonitorLinkID);
		pktSend(&relay);
		return client;
	}

	return NULL;

	
}

SentryClient *sentrySendGetFileContents(char *machine, int iRequestID, char *pFileName, int iMonitorLinkID)
{
	SentryClient	*client;

	client = sentryFindByName(machine);

	if (client)
	{
		if (clientIsServerSideReadOnly(client->name))
		{
			printf("FAILED getting file contents \"%s\" on %s because machine is server-side readonly\n",pFileName,machine);
			filelog_printf("launchcmds.log","FAILED getting file contents \"%s\" on %s because machine is server-side readonly\n",pFileName,machine);
			return NULL;
		}

		if (client->read_only_local)
		{
			printf("FAILED getting file contents \"%s\" on %s because machine is client-side readonly\n",pFileName,machine);
			filelog_printf("launchcmds.log","FAILED getting file contents \"%s\" on %s because machine is client-side readonly\n",pFileName,machine);
			return NULL;
		}
	}

	if (client && client->link)
	{
		Packet *relay = pktCreate(client->link, SENTRYSERVER_GETFILECONTENTS);
		pktSendBits(relay, 32, iRequestID);
		pktSendString(relay, pFileName);
		pktSendBits(relay, 32, iMonitorLinkID);
		pktSend(&relay);
		return client;
	}

	return NULL;
}


SentryClient *sentrySendGetDirectoryContents(char *machine, int iRequestID, char *pDirectoryName, int iMonitorLinkID)
{
	SentryClient	*client;

	client = sentryFindByName(machine);
	if (client && client->link)
	{
		Packet *relay = pktCreate(client->link, SENTRYSERVER_GETDIRECTORYCONTENTS);
		pktSendBits(relay, 32, iRequestID);
		pktSendString(relay, pDirectoryName);
		pktSendBits(relay, 32, iMonitorLinkID);
		pktSend(&relay);
	}

	return client;
}



SentryClient *sentrySendLaunch(char *machine,char *command)
{
	SentryClient	*client;

	client = sentryFindByName(machine);

	if (client)
	{
		if (clientIsServerSideReadOnly(client->name))
		{
			printf("FAILED launch cmd \"%s\" to %s because machine is server-side readonly\n",command,machine);
			filelog_printf("launchcmds.log","FAILED launch cmd \"%s\" to %s because machine is server-side readonly\n",command,machine);
			return NULL;
		}

	
		if (client->read_only_local)
		{
			printf("FAILED launch cmd \"%s\" to %s because machine is client-side readonly\n",command,machine);
			filelog_printf("launchcmds.log","FAILED launch cmd \"%s\" to %s because machine is client-side readonly\n",command,machine);
			return NULL;
		}
	}

	if (client && client->link)
	{
		Packet			*relay = pktCreate(client->link,SENTRYSERVER_LAUNCH);

		printf("sent launch cmd \"%s\" to %s (%s)\n",command,machine,makeIpStr(linkGetIp(client->link)));
		filelog_printf("launchcmds.log","sent launch cmd \"%s\" to %s (%s)\n",command,machine,makeIpStr(linkGetIp(client->link)));
		pktSendString(relay,command);
		pktSend(&relay);
	}
	else
	{
		printf("FAILED launch cmd \"%s\" to %s because machine not found\n",command,machine);
		filelog_printf("launchcmds.log","FAILED launch cmd \"%s\" to %s because machine not found\n",command,machine);
	}
	return client;
}

SentryClient *sentrySendLaunchAndWait(char *machine,char *command)
{
	SentryClient	*client;

	client = sentryFindByName(machine);

	if (client)
	{
		if (clientIsServerSideReadOnly(client->name))
		{
			printf("FAILED launchAndWait cmd \"%s\" to %s because machine is server-side readonly\n",command,machine);
			filelog_printf("launchcmds.log","FAILED launchAndWait cmd \"%s\" to %s because machine is server-side readonly\n",command,machine);
			return NULL;
		}

		if (client->read_only_local)
		{
			printf("FAILED launchAndWait cmd \"%s\" to %s because machine is client-side readonly\n",command,machine);
			filelog_printf("launchcmds.log","FAILED launchAndWait cmd \"%s\" to %s because machine is client-side readonly\n",command,machine);
			return NULL;
		}
	}

	if (client && client->link)
	{
		Packet			*relay = pktCreate(client->link,SENTRYSERVER_LAUNCH_AND_WAIT);

		printf("sent launchAndWait cmd \"%s\" to %s (%s)\n",command,machine,makeIpStr(linkGetIp(client->link)));
		filelog_printf("launchcmds.log","sent launchAndWait cmd \"%s\" to %s (%s)\n",command,machine,makeIpStr(linkGetIp(client->link)));
		pktSendString(relay,command);
		pktSend(&relay);
	}
	else
	{
		printf("FAILED launchAndWait cmd \"%s\" to %s because machine not found\n",command,machine);
		filelog_printf("launchcmds.log","FAILED launchAndWait cmd \"%s\" to %s because machine not found\n",command,machine);
	}
	return client;
}

SentryClient *sentrySendCreateFile(char *machine, char *pFileToCreate, int iCompressedSize, int iUncompressedSize, 
	void *pBuffer)
{
	SentryClient	*client;

	client = sentryFindByName(machine);

	if (client)
	{
		if (clientIsServerSideReadOnly(client->name))
		{
			printf("FAILED createfile \"%s\" to %s because machine is server-side readonly\n",pFileToCreate,machine);
			filelog_printf("launchcmds.log","FAILED createfile \"%s\" to %s because machine is server-side readonly\n",pFileToCreate,machine);
			return NULL;
		}

		if (client->read_only_local)
		{
			printf("FAILED createfile \"%s\" to %s because machine is client-side readonly\n",pFileToCreate,machine);
			filelog_printf("launchcmds.log","FAILED createfile \"%s\" to %s because machine is client-side readonly\n",pFileToCreate,machine);
			return NULL;
		}
	}


	if (client && client->link)
	{
		Packet			*relay = pktCreate(client->link,SENTRYSERVER_CREATEFILE);

		pktSendString(relay,pFileToCreate);
		pktSendBits(relay, 32, iCompressedSize);
		pktSendBits(relay, 32, iUncompressedSize);
		pktSendBytes(relay, iCompressedSize, pBuffer);

		pktSend(&relay);
	}
	return client;
}

void updateTitle()
{
	char	buf[200];
	int		i,active=0,inactive=0,unreg=0;

	for(i=0;i<eaSize(&sentries);i++)
	{
		SentryClient *client = sentries[i];

		if (!client->link)
			inactive++;
		else
			active++;
		if (!client->machine)
			unreg++;
	}
	sprintf(buf,"SentryServer | %d total  %d inactive  %d unregistered",eaSize(&sentries),inactive,unreg);
	setConsoleTitle(buf);
}

char *statGetTitle(char *str,char *dst)
{
	char	*s;

	strcpy_s(dst,32,str);
	s = strchr(dst,'_');
	if (s)
		*s = 0;
	return dst;
}

Stat *statFind(SentryClient *sentry,char *key,U32 uid)
{
	Stat	*stat,stat_search;
	char	*stash_key;

	if (!stashGetKey(stat_label_hashes, key, &stash_key))
		return 0;
	stat_search.key = stash_key;
	stat_search.uid = uid;
	if (!stashFindPointer(sentry->stat_hashes,&stat_search.key,&stat))
		return 0;
	return stat;
}

char *statsGetLabel(char *key)
{
	char	*stash_key;

	if (!stashGetKey(stat_label_hashes, key, &stash_key))
		return 0;
	return stash_key;
}

static void statFormat(Stat *stat,char *str,F64 value)
{
	char	buf[1024];
	int		len;

	stat->value = value;

	if (inline_stricmp(stat->key,"Status_Last")==0 || inline_stricmp(stat->key,"Status_Total")==0)
		printTimeUnit_s(buf,sizeof(buf),MIN(stat->value,999*24*3600));
	else if (inline_stricmp(stat->key,"Disk_Queue")==0)
		sprintf(buf,"%0.2f",stat->value);
	else if (inline_stricmp(stat->key,"Process_Usage")==0 || inline_stricmp(stat->key,"Process_Usage60")==0)
		sprintf(buf,"%0.1f",stat->value);
	else if (inline_stricmp(stat->key,"OS_Build")==0 || inline_stricmp(stat->key,"Process_PID")==0)
		sprintf(buf,"%d",(int)stat->value);
	else if (strnicmp(stat->key,"RAM_",4)==0 || strstri(stat->key,"memory"))
		printUnit_s(buf,sizeof(buf),stat->value);
	else if (stat->value)
	{
		printUnitDecimal_s(buf,sizeof(buf),stat->value);
	}
	else
		strcpy(buf,str);

	len = (int)strlen(buf)+1;
	if (len > stat->str_size)
	{
		free(stat->str);
		stat->str_size = len;
		stat->str = malloc(len);
		*stat->str = 0;
	}
	if (strcmp(stat->str,buf)!=0)
	{
		stat->changed_seconds = 1;
		stat->changed_minutes = 1;
		strcpy_s(stat->str,len,buf);
	}
}

int labelCmp(const char **pa,const char **pb)
{
	int		a_idx=0,b_idx=0;

	stashFindInt(stat_label_hashes, *pa, &a_idx);
	stashFindInt(stat_label_hashes, *pb, &b_idx);
	return a_idx - b_idx;
}

void statFree(Stat *stat)
{
	free(stat->str);
	free(stat);
}

Stat *addStat(SentryClient *client,U32 uid,char *key,char *str,F64 value, bool bKeyOnly)
{
	char	*stash_key;
	Stat	*stat;

	if (!stashGetKey(stat_label_hashes, key, &stash_key))
	{
		static int	idx;

		stashAddInt(stat_label_hashes, key, ++idx, 0);
		stashGetKey(stat_label_hashes, key, &stash_key);
		eaPush(&stat_labels,stash_key);
	}
	
	if (bKeyOnly)
	{
		return NULL;
	}

	stat = statFind(client,stash_key,uid);
	if (!stat)
	{
		stat = calloc(sizeof(*stat),1);
		stat->key	= stash_key;
		stat->uid	= uid;
		eaPush(&client->stats,stat);
		stashAddPointer(client->stat_hashes,&stat->key,stat,1);
	}
	if ((!client->machine || (client->machine->type != Machine_Reserved && client->machine->type != Machine_Open)) && inline_stricmp(stat->key,"Process_Title")==0)
		str = "Blocked";
	statFormat(stat,str,value);

	if (inline_stricmp(stat->key,"Net_LocalIp")==0)
		client->local_ip = inet_addr(stat->str);
	if (inline_stricmp(stat->key,"Net_PublicIp")==0)
		client->public_ip = inet_addr(stat->str);

	return stat;
}

Stat *addStatVal(SentryClient *client,U32 uid,char *key,F64 val)
{
	char	buf[100];

	sprintf(buf,"%f",val);
	return addStat(client,uid,key,buf,val, false);
}

void sentryConnect(NetLink *link,SentryClient *client)
{
}

void sentryDisconnect(NetLink *link,SentryClient **client_p)
{
	SentryClient	*client;
	int				i;

	if (!client_p)
		return;
	client = *client_p;
	if (!client || eaFind(&sentries,client) < 0)
		return;


	if (clientIsAlertOnDisconnect(client->name))
	{
		checkIfClientRemainsDisconnected(client->name);

	}
	

	client->link = 0;

	for(i=eaSize(&client->stats)-1;i>=0;i--)
	{
		Stat	*stat = client->stats[i];

		if (!stat->permanent)
		{
			Stat	*val;

			stashRemovePointer(client->stat_hashes,&stat->key,&val);
			statFree(stat);
			eaRemove(&client->stats,i);
		}
	}
	printf("disconnect from %s [%p]\n",client->name,link);
	if (!client->machine)
	{
		stashTableDestroy(client->stat_hashes);
		free(client);
		eaFindAndRemove(&sentries,client);
	}
	updateTitle();
}

void handleConnect(Packet *pkt,NetLink *link,SentryClient **client_p)
{
	SentryClient	*client=0;
	char			name[200],ip_str[100];
	int				i;

	pktGetString(pkt,name,sizeof(name));
	for(i=0;i<eaSize(&sentries);i++)
	{
		if (inline_stricmp(sentries[i]->name,name)==0)
			client = sentries[i];
	}
	if (client && client->link)
	{
		NetLink *oldDelink;
		NetLink	*delink = link;
		Packet	*pak;
		char	ip_buf[100];

		delink = client->link;
		linkSetUserData(client->link,0);
		filelog_printf("svr_quitlog.log","Removed dead connection from %s (%s) [%p]\n",client->name,linkGetIpStr(delink,ip_buf,sizeof(ip_buf)),delink);
		printf("Removed dead connection from %s [%p]\n",client->name,delink);

		pak = pktCreate(delink,SENTRYSERVER_DUPQUIT);
		pktSend(&pak);
		oldDelink = delink;
		linkFlushAndClose(&delink, "Removed dead connection");

		if (oldDelink == link)
			return;
	}
	if (!client)
	{
		client = calloc(sizeof(*client),1);
		strcpy(client->name,name);
		client->stat_hashes = stashTableCreate(1000,StashDefault, StashKeyTypeFixedSize, sizeof(void*) + sizeof(U32));
		eaPush(&sentries,client);
		addStat(client,0,"Status_Type","X",0, false);
	}
	client->link = link;
	client->connect_count++;
	client->done_patching = 0;

	*client_p = client;
	addStatVal(client,0,"Status_Connect",client->connect_count);

	linkGetIpStr(link, ip_str, sizeof(ip_str));
	addStat(client,0,"Status_IP",ip_str,0, false);
	printf("Connect from %s [%p]\n",client->name,link);
	updateTitle();
	client->first_heard = timeSecondsSince2000();
}

static F64 logStat(char *sample_type,SentryClient *sentry,Stat *stat,F64 *values)
{
	int		j;
	F64		value=0;
	char	*s,title[200],fname[MAX_PATH],date[200],*client_name;

	for(j=0;j<60;j++)
		value += values[j];
	statGetTitle(stat->key,title);
	timeMakeLocalDateNoTimeString(date);
	if (inline_stricmp(sample_type,"hours")==0)
	{
		s = strrchr(date,'-');
		if (s)
			*s = 0;
	}
	if (sentry->name[0])
		client_name = sentry->name;
	else
		client_name = sentry->stats[0]->str;
	sprintf(fname,"%s/%s/%s",sample_type,date,title);
	if (value)
		filelog_printf(fname,"\"%s\" %s %f %d",client_name,stat->key,value,stat->uid);
	else
		filelog_printf(fname,"\"%s\" %s \"%s\" %d",client_name,stat->key,stat->str,stat->uid);
	return value;
}

void storeStats(SentryClient *client)
{
	int		i,idx,min_idx;

	idx = client->tag % 60;

	for(i=0;i<eaSize(&client->stats);i++)
	{
		Stat	*stat = client->stats[i];

		stat->seconds[idx] = stat->value;
	}
	if (idx != 59)
		return;
	min_idx = (client->tag / 60) % 60;
	for(i=1;i<eaSize(&client->stats);i++)
	{
		Stat	*stat = client->stats[i];

		if (!stat->changed_seconds)
			continue;
		stat->changed_seconds = 0;
		stat->minutes[min_idx] = logStat("minutes",client,stat,stat->seconds);
	}

	if (min_idx != 59)
		return;
	for(i=1;i<eaSize(&client->stats);i++)
	{
		Stat	*stat = client->stats[i];

		if (!stat->changed_minutes)
			continue;
		stat->changed_minutes = 0;
		logStat("hours",client,stat,stat->minutes);
	}
}



void handleStats(Packet *pkt,SentryClient **client_p)
{
	char			*key,*str;
	U32				uid,tag;
	F64				val;
	int				i;
	SentryClient	*client;

	if (!client_p)
		return;
	client = *client_p;
	if (!client)
		return;
	storeStats(client);
	tag = ++client->tag;
	while(!pktEnd(pkt))
	{
		uid = pktGetU32(pkt);
		key = pktGetStringTemp(pkt);
		str	= pktGetStringTemp(pkt);
		val = pktGetF64(pkt);

		//special case handling for ReadOnly_Local, so we can stick it in a nice
		//easy-to-find place. Also put it in a stat as normal so the normal logging/searching work
		if (stricmp(key, "ReadOnly_Local") == 0)
		{
			if (val)
			{
				client->read_only_local = 1;
			}
			else
			{
				client->read_only_local = 0;
			}
		}

		addStat(client,uid,key,str,val, false)->tag = tag;
	}

	if (clientIsServerSideReadOnly(client->name))
	{
		addStat(client, 0, "ReadOnly_ServerSide", "1", 1, false)->tag = tag;
	}
	else
	{
		addStat(client, 0, "ReadOnly_ServerSide", "0", 0, false)->tag = tag;
	}



	for(i=eaSize(&client->stats)-1;i>=0;i--)
	{
		Stat	*stat = client->stats[i];
	
		if (stat->uid && stat->tag != tag)
		{
			Stat	*unused;

			stashRemovePointer(client->stat_hashes,&stat->key,&unused);
			statFree(stat);
			eaRemove(&client->stats,i);
		}
	}
	client->last_heard = timeSecondsSince2000();


}

void handleAutopatch(Packet *pkt,NetLink *link,SentryClient *client)
{
	static U8	*exe_mem,*exe_zip;
	static U32	exe_size,exe_zip_size,exe_md5[4];

	static U8	*exe_mem64,*exe_zip64;
	static U32	exe_size64,exe_zip_size64,exe_md564[4];


	U32			size,protocol_ver,md5[4];
	char *pExeName = NULL;
	char *pExeName64 = NULL;
	Packet		*pak;

	if (!client)
		return;
	protocol_ver = pktGetU32(pkt);
	size = pktGetU32(pkt);
	pktGetBytes(pkt,sizeof(md5),md5);

	if (!exe_mem)
	{
		if (g_sentry_name)
			estrCopy2(&pExeName,g_sentry_name);
		else
		{
			estrGetDirAndFileName(getExecutableName(), &pExeName, NULL);
			estrConcatf(&pExeName, "%s", "/sentry.exe");
		}

		estrCopy(&pExeName64, &pExeName);
		estrReplaceOccurrences(&pExeName64, ".exe", "X64.exe");

		exe_mem = fileAlloc(pExeName,&exe_size);
		assertmsgf(exe_mem, "Can't find local sentry.exe for autopatching, this is totally unacceptable");

		exe_zip = zipData(exe_mem,exe_size,&exe_zip_size);

		cryptMD5Init();
		cryptMD5Update(exe_mem,exe_size);
		cryptMD5Final(exe_md5);

		exe_mem64 = fileAlloc(pExeName64,&exe_size64);
		assertmsgf(exe_mem64, "Can't find local sentryX64.exe for autopatching, this is totally unacceptable");

		exe_zip64 = zipData(exe_mem64,exe_size64,&exe_zip_size64);

		cryptMD5Init();
		cryptMD5Update(exe_mem64,exe_size64);
		cryptMD5Final(exe_md564);


		estrDestroy(&pExeName);
		estrDestroy(&pExeName64);
	}
	
	if ((exe_size == size && memcmp(exe_md5,md5,sizeof(md5))==0)
		|| (exe_size64 == size && memcmp(exe_md564, md5, sizeof(md5)) == 0))
	{
		pak = pktCreate(link,SENTRYSERVER_AUTOPATCH);
		pktSendU32(pak,1);
		pktSend(&pak);
	}
	else
	{
		//send the x64 first in a separate packet, because during the switchover from 32-bit-only to 64-bit we
		//need to have the old 32 bit versions patch to the new 32-bit versions that support x64, so the old AUTOPATCH packet should
		//remain the same
		printf("Exes and CRCs DO NOT MATCH, attempting patching\n");
		pak = pktCreate(link, SENTRYSERVER_AUTOPATCH_X64FILES);
		pktSendZippedAlready(pak, exe_size64, exe_zip_size64, exe_zip64);
		pktSend(&pak);


		pak = pktCreate(link,SENTRYSERVER_AUTOPATCH);
		pktSendU32(pak,2);
		pktSendZippedAlready(pak,exe_size,exe_zip_size,exe_zip);
		pktSend(&pak);
	}

}

static void handleHereIsFileCRC(Packet *pak)
{
	int iRequestID = pktGetBits(pak, 32);
	int iMonitorLinkID = pktGetBits(pak, 32);
	U32 iCRC = pktGetBits(pak, 32);
	NetLink *pOutLink = GetMonitoringLinkFromID(iMonitorLinkID);

	if (pOutLink)
	{
		Packet *pOutPack = pktCreate(pOutLink, MONITORSERVER_HEREISFILECRC);
		pktSendBits(pOutPack, 32, iRequestID);
		pktSendBits(pOutPack, 32, iCRC);
		pktSend(&pOutPack);
	}
}

static void handleHereIsFileContents(Packet *pak, SentryClient *pClient)
{
	int iRequestID = pktGetBits(pak, 32);
	int iMonitorLinkID = pktGetBits(pak, 32);
	char *pFileName = pktGetStringTemp(pak);
	NetLink *pOutLink = GetMonitoringLinkFromID(iMonitorLinkID);

	if (pOutLink)
	{
		Packet *pOutPack;
		int iSize = pktGetBits(pak, 32);
		FileContents_FromSimpleQuery contents = {0};
		void *pBuf;
		if (iSize)
		{
			pBuf = pktGetBytesTemp(pak, iSize);

			contents.pContents = TextParserBinaryBlock_CreateFromMemory(pBuf, iSize, false);
		}

		contents.iQueryID = iRequestID;
		contents.pFileName = pFileName;
		contents.pMachineName = pClient ? pClient->name : "UNKNOWN__SENTRY_SERVER_CORRUPTION";

		pOutPack = pktCreate(pOutLink, MONITORSERVER_GETFILECONTENTS_RESPONSE);
		ParserSendStructSafe(parse_FileContents_FromSimpleQuery, pOutPack, &contents);
		pktSend(&pOutPack);

		StructDestroy(parse_TextParserBinaryBlock, contents.pContents);
	}


}


static void handleHereAreDirectoryContents(Packet *pak)
{
	int iRequestID = pktGetBits(pak, 32);
	int iMonitorLinkID = pktGetBits(pak, 32);
	char *pDirectoryContents = pktGetStringTemp(pak);
	NetLink *pOutLink = GetMonitoringLinkFromID(iMonitorLinkID);

	if (pOutLink)
	{
		Packet *pOutPack = pktCreate(pOutLink, MONITORSERVER_HEREAREDIRECTORYCONTENTS);
		pktSendBits(pOutPack, 32, iRequestID);
		pktSendBits(pOutPack, 1, 1);
		pktSendString(pOutPack, pDirectoryContents);
		pktSend(&pOutPack);
	}
}


void sentryMsg(Packet *pkt,int cmd,NetLink *link,SentryClient **client_p)
{
	SentryClient	*client=0;

	if (client_p)
		client = *client_p;
	switch(cmd)
	{
		xcase SENTRYCLIENT_CONNECT:
			handleConnect(pkt,link,client_p);
		xcase SENTRYCLIENT_STATS:
			handleStats(pkt,client_p);
		xcase SENTRYCLIENT_AUTOPATCH:
			handleAutopatch(pkt,link,client);
		xcase SENTRYCLIENT_AUTOPATCH_DONE:
			if (client)
				client->done_patching = 1;
		xcase SENTRYCLIENT_HEREISFILECRC:
			handleHereIsFileCRC(pkt);
		xcase SENTRYCLIENT_HEREISFILECONTENTS:
			handleHereIsFileContents(pkt, client);
		xcase SENTRYCLIENT_HEREAREDIRECTORYCONTENTS:
			handleHereAreDirectoryContents(pkt);
	}
}

static Stat *addPermanentStat(SentryClient *client,U32 uid,char *key,char *str,F64 value)
{
	Stat *stat = addStat(client,uid,key,str,value, false);

	if (stat)
		stat->permanent = 1;
	return stat;
}

void sentryListen(NetComm *comm,char *sentry_name)
{
	int		i;

	g_sentry_name = sentry_name;
	stat_label_hashes = stashTableCreateWithStringKeys(4,  StashDeepCopyKeys_NeverRelease );

	//special case... always add "Machine" no matter what in slot zero, so that queries still work
	//when sentryCfg.txt is open
	addStat(NULL,0,"Machine",NULL,0, true);

	for(i=0;i<eaSize(&machine_list->machine);i++)
	{
		SentryClient	*client;
		SentryServerMachineType		type;

		client = calloc(sizeof(*client),1);
		client->machine = machine_list->machine[i];
		strcpy(client->name,machine_list->machine[i]->name);
		client->stat_hashes = stashTableCreate(10000,StashDefault, StashKeyTypeFixedSize, sizeof(void*) + sizeof(U32));
		eaPush(&sentries,client);
		addPermanentStat(client,0,"Machine",client->name,0);
		type = client->machine->type;
		if (type == Machine_User)
			addPermanentStat(client,0,"Status_Type","User",0);
		else if (type == Machine_Open)
			addPermanentStat(client,0,"Status_Type","Open",0);
		else if (type == Machine_Reserved)
			addPermanentStat(client,0,"Status_Type","Reserved",0);
		else
			addPermanentStat(client,0,"Status_Type","X",0);
		addPermanentStat(client,0,"Status_Last","0",0);
		addPermanentStat(client,0,"Status_Total","0",0);
	}

	commListen(comm,LINKTYPE_UNSPEC, 0,SENTRYSERVER_PORT,sentryMsg,sentryConnect,sentryDisconnect,sizeof(SentryClient *));
}

void sentryUpdate()
{
	int				i;
	F64				seconds,curr_seconds = timeSecondsSince2000();
	SentryClient	*client;

	for(i=0;i<eaSize(&sentries);i++)
	{
		client = sentries[i];

		seconds = curr_seconds - client->last_heard;
		addStatVal(sentries[i],0,"Status_Last",seconds);
		seconds = curr_seconds - client->first_heard;
		if (!client->link)
			seconds = 0;
		addStatVal(sentries[i],0,"Status_Total",seconds);
		addStat(sentries[i],0,"Status_Connected",client->link ? "Yes" : "No",0, false);
	}
}

#define CLIENT_DISCONNECT_TIME_BEFORE_ALERT 10

void ClientRemainsDisconnectedCB(TimedCallback *callback, F32 timeSinceLastCallback, char *pClientName)
{
	if (!sentryFindByName(pClientName))
	{
		printf("Triggering an alert because %s disconnected for %d seconds\n", pClientName, CLIENT_DISCONNECT_TIME_BEFORE_ALERT);
		CRITICAL_NETOPS_ALERT("SENTRY_DISCONNECT", "Sentry lost contact with %s, which is on the alert-on-disconnect list",
			pClientName);
	}
}

void checkIfClientRemainsDisconnected(const char *pClientName)
{
	TimedCallback_Run(ClientRemainsDisconnectedCB, (void*)allocAddString(pClientName), CLIENT_DISCONNECT_TIME_BEFORE_ALERT);
}