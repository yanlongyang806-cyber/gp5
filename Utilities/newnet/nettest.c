#include "sock.h"
#include "net.h"
#include "timing.h"
#include "earray.h"
#include "estring.h"
#include "file.h"
#include "utils.h"

NetComm	*comm;

typedef struct
{
	int		num_monkeys;
} NetTestClient;

#include "netprivate.h"
void netTestConnect(NetLink *link,NetTestClient *client)
{
	linkSendRecvSize(link,500000,0);
	printf("connect %d\n",eaSize(&link->listen->links));
}

void netTestDisconnect(NetLink *link,NetTestClient *client)
{
	printf("disconnect %d monkeys\n",client ? client->num_monkeys : 0);
}

void netTestMsg(Packet *pkt,int cmd,NetLink *link,NetTestClient *client)
{

	if (cmd == 1)
	{
		char	monkey_buf[100],zebra_buf[100],bytes_buf[100];
		int		t,val;
		F32		f;

		t = pktEnd(pkt);
		pktGetString(pkt,monkey_buf,sizeof(monkey_buf));
		val = pktGetBits(pkt,32);
		pktGetString(pkt,zebra_buf,sizeof(zebra_buf));
		f = pktGetF32(pkt);
		pktGetBytes(pkt,5,bytes_buf);
		t = pktEnd(pkt);
		val = pktGetBits(pkt,9);
		t = pktEnd(pkt);
		if (client)
			client->num_monkeys++;
		if (linkIsServer(link))
		{
			pkt = pktCreate(link,2);
			pktSend(&pkt);
			linkFlush(link);
		}
	}
}

void httpSendStr(NetLink *link,char *str)
{
	Packet	*pak = pktCreateRaw(link);
	char	length_buf[100];

	sprintf(length_buf,"Content-Length: %d\n",strlen(str));

	pktSendStringRaw(pak,"HTTP/1.1 200 OK\n");
	pktSendStringRaw(pak,length_buf);
	pktSendStringRaw(pak,"\n");
	pktSendStringRaw(pak,str);
	pktSendRaw(&pak);
}

void httpSendStr2(NetLink *link,char *str)
{
	Packet	*pak = pktCreateRaw(link);
	//char	length_buf[100];

	pktSendStringRaw(pak,"\n");
	pktSendStringRaw(pak,str);
	pktSendRaw(&pak);
}

void httpMsg(Packet *pkt,int cmd,NetLink *link,NetTestClient *client)
{
	char	*data,*url,*s;
	static int		count=0;

	data = pkt->data;
	if (strnicmp(data,"GET",3)==0)
	{
		url = data+4;
		s = strchr(url,'\r');
		if (s)
			*s = 0;
		s = strrchr(url,' ');
		if (s)
			*s = 0;
	}
	{
		char	*estr=0;

		estrConcatf(&estr,"<META HTTP-EQUIV=\"Refresh\" CONTENT=\"2\">\n");
		estrConcatf(&estr,"hello world %d\n",count++);
		httpSendStr(link,estr);
		estrDestroy(&estr);
	}
#if 0
	pak = pktCreate();
	if (1)
	{
		estrConcatf(&estr,"<html><body bgcolor=#505050> <font color=#ffaa22>\n");
		for(j=0;j<1000;j++)
		{
			for(i=0;i<30;i++)
			{
				estrConcatf(&estr,"%d ",i+j*30);
			}
			estrConcatf(&estr,"\n\n");
		}
		estrConcatf(&estr,"</body></html>\n");

		estrConcatf(&estr,"<form name=\"input\"\n");
		estrConcatf(&estr,"method=\"get\">\n");
		estrConcatf(&estr,"Command \n");
		estrConcatf(&estr,"<input type=\"text\" name=\"cmd\" value=\"zombie\" size=40>\n");
		estrConcatf(&estr,"<br><input type =\"submit\" value =\"List all commands\">\n");
		estrConcatf(&estr,"</form>\n\n");

		sprintf(length_buf,"Content-Length: %d\n",estrLength(&estr));
		pktSendStringRaw(pak,"HTTP/1.1 200 OK\n");
		//pktSendStringRaw(pak,"Server: Cryptic\n");
		pktSendStringRaw(pak,length_buf);
		pktSendStringRaw(pak,"\n");
		pktSendStringRaw(pak,estr);
		estrDestroy(&estr);
	}
	else
	{
		static int i;
		char	buf[100];

		sprintf(buf,"srvr %d\n",i);
		pktSendString(pak,buf);
	}
	printf("\nsent %d bytes  total sent %d\n",pak->stream.size,total);
	total += pak->stream.size;
	pktSend(&pak,link);
	lnkBatchSend(link);
	//netRemoveLink(link);
	return 1;

	reply = pktCreateRaw(link);
	sprintf(content_length,"Content-length: %d\r\n\r\n",12);
	pktSendStringRaw(reply,content_length);
	pktSendStringRaw(reply,"Hello world\n");
	pktSendRaw(&reply);
	//linkRemove(&link);
#endif
}

void dataTest(NetLink *link)
{
	Packet	*pkt = pktCreate(link,1);
	pktSendString(pkt,"monkey");
	pktSendBits(pkt,32,1234567);
	pktSendString(pkt,"zebra");
	pktSendF32(pkt,4.2);
	pktSendBytes(pkt,5,"five5");
	pktSendBits(pkt,9,43);
	pktSend(&pkt);
}

#include "mathutil.h"
void randTest(NetLink *link)
{
	int		i;
	Packet	*pkt = pktCreate(link,1);

	for(i=0;i<100;i++)
		pktSendBits(pkt,32,randInt(999));
	pktSend(&pkt);
}

void test(char *ip,int port)
{
	NetLink		*link=0;
	U32			last_pkt_sent=0,last_bytes_sent=0,last_real_bytes=0;
	int			i,timer = timerAlloc(),count=0;
	char		str[10000] = "12 monkeys!\r\n\r\n";
	F32			dt;
	int			server = !port;
	char		buffer[] = "monkey";
	NetListen	*nl;

	comm = commCreate(server * 20,1);
	//commTimedDisconnect(comm,10);
	//commRandomDisconnect(comm,10000);

	if (server)
	{
		nl = commListen(comm,LINK_RAW,8080,httpMsg,netTestConnect,netTestDisconnect,sizeof(NetTestClient));
		nl = commListen(comm,0,8081,netTestMsg,netTestConnect,netTestDisconnect,sizeof(NetTestClient));
		for(;;)
			commMonitor(comm);
	}
	else
	{
		link = commConnect(comm,0,ip,port,netTestMsg,netTestConnect,netTestDisconnect,0);
		linkSendRecvSize(link,500000,0);
		linkFlushLimit(link,100000);
		//linkSetMaxSend(link,1000);
		//linkSetTimeout(link,5);
		for(i=0;i<1400;i++)
			str[i] = 'x';
	//	strcat(str,"\r\n\r\n");
		for(;;)
		{
			if (linkConnected(link))
			{
				if (!linkSendBufFull(link))
				{
					for(i=0;i<1;i++)
					{
						linkCompress(link,1);//++count & 1);
						dataTest(link);
					}
					//linkFlush(link);
					if ((dt=timerElapsed(timer)) > 1)
					{
						const LinkStats *stats = linkStats(link);
						int packets = stats->send.packets - last_pkt_sent;
						U32 bytes = stats->send.bytes - last_bytes_sent;
						U32 compressed = stats->send.real_bytes - last_real_bytes;

						printf("%.1f msgs/sec   %.1fM B/sec   %.2fM CB/sec   elapsed %f\n",packets/dt,bytes / (dt * 1000000.0),compressed / (dt * 1000000.0),linkRecvTimeElapsed(link));
						timerStart(timer);
						last_pkt_sent = stats->send.packets;
						last_bytes_sent = stats->send.bytes;
						last_real_bytes = stats->send.real_bytes;
					}
				}
				else
					Sleep(1);
			}
			commMonitor(comm);

			if (0)// && randInt(10000) == 0)
			{
				linkRemove(&link);
				link = commConnect(comm,0,ip,port,netTestMsg,netTestConnect,netTestDisconnect,0);
				linkFlushLimit(link,5000);
			}
			if (linkDisconnected(link))
			{
				printf("reconnecting..\n");
				free(link);
				link = commConnect(comm,0,ip,port,netTestMsg,netTestConnect,netTestDisconnect,0);
				linkFlushLimit(link,5000);
				linkSendRecvSize(link,100000,0);
				//linkSetTimeout(link,5);
			}
			Sleep(0);
		}
	}
}

void main(int argc,char **argv)
{
	if (argc == 2)
		test("127.0.0.1",atoi(argv[1]));
	else if (argc == 3)
		test(argv[1],atoi(argv[2]));
	else
		test(0,0);
}
