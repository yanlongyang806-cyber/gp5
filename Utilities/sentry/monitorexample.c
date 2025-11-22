#include "net/net.h"
#include "utils.h"
#include "sentry_comm.h"
#include "CrypticPorts.h"

extern NetComm *comm;

void handleMsg(Packet *pkt)
{
	printf("result: %s\n",pktGetStringTemp(pkt));
}

void handleQueryResult(Packet *pkt)
{
	int		count;
	char	*args[20];
	F64		value;

	while(!pktEnd(pkt))
	{
		count = tokenize_line(pktGetStringTemp(pkt),args,0);
		if (!count)
			printf("\n");
		else
		{
			value = pktGetF64(pkt);
			printf("\t%s",args[1]);
		}
	}
	printf("\n");
}

void monitorMsg(Packet *pkt,int cmd,NetLink *link,void *user_data)
{
	switch(cmd)
	{
		xcase MONITORSERVER_MSG:
			handleMsg(pkt);
		xcase MONITORSERVER_QUERY:
			handleQueryResult(pkt);
	}
}

void sendQuery(NetLink *link,char *query)
{
	Packet	*pkt = pktCreate(link,MONITORCLIENT_QUERY);

	pktSendString(pkt,query);
	pktSend(&pkt);
}

void sendLaunch(NetLink *link)
{
	Packet	*pkt = pktCreate(link,MONITORCLIENT_LAUNCH);

	pktSendString(pkt,"brogers");
	pktSendString(pkt,"cmd.exe");
	pktSend(&pkt);
}

void monitorExample()
{
	NetLink	*link;

	if (!(link = commConnectWait(comm,LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,"localhost",SENTRYSERVERMONITOR_PORT,monitorMsg,0,0,0,20)))
		return;
	sendLaunch(link);
	sendQuery(link,"machine= status");
	for(;;)
		commMonitor(comm);
}
