/*

To use it, on PC just run "nettest" and it'll start listening.
On any one of the same PC, another PC, or an Xbox, run "nettest <host PC's IP> 8081" 
and it should connect and start sending packets.

It looks like there are a few other tests #ifdef'd out, but as long as it 
connects, handshakes, and gets some packets going across the link, 
that should be validation that it's working.

*/

#include "sock.h"
#include "net.h"
#include "timing.h"
#include "earray.h"
#include "estring.h"
#include "file.h"
#include "utils.h"
#include "mathutil.h"
#include "cmdparse.h"
#include "netprivate.h"
#include "sysutil.h"

typedef struct
{
	int		num_monkeys;
} NetTestClient;


#if 1

#include "CrypticPorts.h"
#include "PerformanceCounter.h"
#include "gimmeDLLWrapper.h"
#include "logging.h"
#include "Organization.h"

int		server_ports[] = {	80, 80,
							443, 443,
							DEFAULT_PATCHSERVER_PORT, DEFAULT_PATCHSERVER_PORT,
							STARTING_GAMESERVER_PORT, STARTING_GAMESERVER_PORT,
							MAX_GAMESERVER_PORT, MAX_GAMESERVER_PORT,
							PUBLIC_PORT_END-1, PUBLIC_PORT_END-1,
							80 };
int		*ports = server_ports;
int		port_count = ARRAY_SIZE(server_ports);
int		xfer_time = 3,xfer_count = 500,xfer_size = 1600;
U32		ether_send_sec;
U32		ether_recv_sec;

void getNetPerfCounters() 
{ 
#if !PLATFORM_CONSOLE
	static PerformanceCounter *counterNetwork=NULL;

	static int inited=0;
	if (!inited) { 
		inited = 1; 
		counterNetwork = performanceCounterCreate("Network Interface"); 
		if (counterNetwork) { 
			performanceCounterAdd(counterNetwork, "Bytes Sent/sec", &ether_send_sec); 
			performanceCounterAdd(counterNetwork, "Bytes Received/sec", &ether_recv_sec); 
		} 
	} 

	if (counterNetwork) 
		performanceCounterQuery(counterNetwork, NULL);
#endif
}


void quit()
{
#undef exit
	char	buf[1000];

	printf("hit return to exit\n");
	gets(buf);
	exit(0);
}

#define VERSION 5

void lagtestMsg(Packet *pkt,int cmd,NetLink *link,NetTestClient *client)
{
	int		buf[1000];
	int		i,sendbytes;

	if (cmd == 1)
	{
		pkt = pktCreate(link,1);
		pktSendU32(pkt,VERSION);
		pktSendU32(pkt,port_count);
		for(i=0;i<port_count;i++)
			pktSendU32(pkt,ports[i]);
		pktSendU32(pkt,xfer_time);
		pktSendU32(pkt,xfer_size);
		pktSendU32(pkt,xfer_count);
		pktSendU32(pkt,link->addr.sin_addr.s_addr);
		pktSend(&pkt);
	}
	else if (cmd == 2)
	{
		sendbytes = pktGetU32(pkt);
		if (sendbytes > sizeof(buf)-4 || sendbytes < 0)
			return;
		for(i=sendbytes/4;i>=0;i--)
			buf[i] = rule30Rand();
		pkt = pktCreate(link,2);
		pktSendBytes(pkt,sendbytes,buf);
		pktSend(&pkt);
	}
	else if (cmd == 3)
	{
		char *s = pktGetStringTemp(pkt);

		log_printf(LOG_CLIENT_PERF,"%s",s);
	}
}


void lagtestServer()
{
	NetComm	*comm;
	int		i;

	comm = commCreate(200000,1);
	commSetPacketReceiveMsecs(comm,200000);
	for(i=0;i<port_count;i++)
		commListenBoth(comm, LINKTYPE_TOUNTRUSTED_20MEG, LINK_FORCE_FLUSH|LINK_NO_COMPRESS, ports[i], lagtestMsg, 0, 0, 0, 0, 0);
	for(;;)
		commMonitor(comm);
}

int g_packets_received;
U32 ip_addr;

void lagclientMsg(Packet *pkt,int cmd,NetLink *link,NetTestClient *client)
{
	if (cmd == 1)
	{
		int		i,version;

		version = pktGetU32(pkt);
		if (version != VERSION)
		{
			printf("nettest server is a newer version, you have to get a new client to run the test\n");
			quit();
		}
		port_count = pktGetU32(pkt);
		ports = calloc(port_count, sizeof(U32));
		for(i=0;i<port_count;i++)
		{
			ports[i] = pktGetU32(pkt);
		}
		xfer_time = pktGetU32(pkt);
		xfer_size = pktGetU32(pkt);
		xfer_count = pktGetU32(pkt);
		ip_addr = pktGetU32(pkt);
	}
	g_packets_received++;
	//client->num_monkeys++;
}

char	server[1000] = ORGANIZATION_DOMAIN;
int		beserver;

AUTO_CMD_STRING(server,server);
AUTO_CMD_INT(beserver,beserver);

char *estr;

void echoPrintf(char const *fmt, ...)
{
	va_list ap;
	int		t;

	va_start(ap, fmt);

	t = estr ? (int)strlen(estr) : 0;
	estrConcatfv(&estr,fmt,ap);
	printf("%s",estr+t);
	va_end(ap);
}


#undef fflush
void lagtestClient()
{
	NetLink	*link;
	int		i,j,got_data,timer,num_samples = 100,ping_time;
	Packet	*pkt;

	getNetPerfCounters();
	timer = timerAlloc();
	printf("contacting nettest server..");
	fflush(stdout);
	link = commConnectWait(commDefault(),LINKTYPE_TOUNTRUSTED_20MEG, LINK_FORCE_FLUSH, server, ports[0],lagclientMsg,0,0,sizeof(NetTestClient),5);
	if (!link)
	{
		printf("timed out\n");
		quit();
	}
	printf("\n");
	pkt = pktCreate(link,1);
	pktSend(&pkt);
	for(;;)
	{
		if (timerElapsed(timer) > 5)
		{
			echoPrintf("timed out\n");
			quit();
		}
		commMonitor(commDefault());
		if (g_packets_received > 0)
			break;
	}
	echoPrintf("Local IP: %s\n",makeIpStr(ip_addr));

	// PING
	ping_time = xfer_time;
	echoPrintf("Ping:");
	fflush(stdout);
	g_packets_received = 0;
	timerStart(timer);
	for(i=0;timerElapsed(timer) < ping_time;i++)
	{
		pkt = pktCreate(link,2);
		pktSendU32(pkt,1);
		pktSend(&pkt);
		for(;;)
		{
			if (timerElapsed(timer) > ping_time)
				break;
			commMonitor(commDefault());
			if (g_packets_received > i)
				break;
		}
	}
	if (i > 0)
		echoPrintf(" %.1f msec\n",(1000 * timerElapsed(timer) / i));
	linkRemove(&link);


	// Per-port bandwidth
	for(j=0;j<port_count;j++)
	{
		echoPrintf("Port: %6d: ",ports[j]);
		fflush(stdout);
		link = commConnectWait(commDefault(),LINKTYPE_TOUNTRUSTED_20MEG, LINK_FORCE_FLUSH|LINK_NO_COMPRESS, server, ports[j],lagclientMsg,0,0,sizeof(NetTestClient),3);
		if (!link)
		{
			echoPrintf("timed out\n");
			continue;
		}
		getNetPerfCounters();
		got_data = 0;
		g_packets_received = 0;
		timerStart(timer);
		for(i=0;i<xfer_count;i++)
		{
			pkt = pktCreate(link,2);
			pktSendU32(pkt,xfer_size);
			pktSend(&pkt);
		}
		for(;;)
		{
			if (timerElapsed(timer) > xfer_time * xfer_count)
				break;
			commMonitor(commDefault());
			if (g_packets_received >= i)
				break;
			if (g_packets_received && !got_data)
			{
				got_data = 1;
				timerStart(timer);
				getNetPerfCounters();
			}
		}
		if (i > 0)
		{
			F32	elapsed;

			getNetPerfCounters();
			elapsed = timerElapsed(timer);
			echoPrintf("  %5.0f KB/sec",((xfer_size * g_packets_received) /  elapsed) / 1000);
			echoPrintf("  %5.0f KB/sec",(F32)ether_send_sec / 1000);
			echoPrintf("  %5.0f KB/sec",(F32)ether_recv_sec / 1000);
			echoPrintf(" %d",g_packets_received);
		}
		else
			echoPrintf(" timeout");
		echoPrintf("\n");
		if (j != port_count-1)
			linkRemove(&link);
	}
	getNetPerfCounters();
	timerStart(timer);
	Sleep(1000);
	getNetPerfCounters();
	echoPrintf("Idle NIC bandwidth  Send: %5.0f KB/sec   Recv: %5.0f KB/sec\n",(F32)ether_send_sec / 1000,(F32)ether_recv_sec / 1000);

	pkt = pktCreate(link,3);
	pktSendString(pkt,estr);
	pktSend(&pkt);
	linkFlushAndClose(&link,"");
	Sleep(1000);
	quit();
}


int wmain(int argc, S16 **argv_wide)
{
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV

	DO_AUTO_RUNS

	gimmeDLLDisable(1);
	cmdParseCommandLine(argc,argv);

	if (argc == 2)
		lagtestServer();
	else
		lagtestClient();
    return 0;
	EXCEPTION_HANDLER_END
}

#else
NetComm	*comm;

typedef struct
{
	int		num_monkeys;
} NetTestClient;

#include "netprivate.h"

void netTestDisconnect(NetLink *link,NetTestClient *client)
{
    if(link->error)
        printf("error = %s\n", link->error);

    if(link->disconnectReason)
        printf("disconnectReason = %s\n", link->disconnectReason);

	if (!client->num_monkeys)
		printf("-----------------------------> got no packets\n");

    printf("disconnect %d packets\n",client ? client->num_monkeys : 0);
}

void netTestMsg(Packet *pkt,int cmd,NetLink *link,NetTestClient *client)
{
	if (client)
		client->num_monkeys++;

	printf("cmd %d\n",cmd);
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
#if 1
		if (linkIsServer(link))
		{
			pkt = pktCreate(link,2);
			pktSend(&pkt);
			linkFlush(link);
			linkFlushAndClose(&link,0);
		}
#endif
	}
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
#if 0
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
#endif
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
	Packet	*pkt = pktCreate(link,2);

	for(i=0;i<100;i++)
		pktSendBits(pkt,32,randInt(999));
	pktSend(&pkt);
}

void netTestServerConnect(NetLink *link,NetTestClient *client)
{
	linkSetTimeout(link,0);
	linkFlushLimit(link,1000);
	printf("connect %d\n",eaSize(&link->listen->links));

}

void netTestClientConnect(NetLink *link,NetTestClient *client)
{
	linkSetTimeout(link,0);
	linkFlushLimit(link,1000);
	printf("connect %d\n",eaSize(&link->listen->links));

	//randTest(link);
	//printf("flush and close\n");
	//linkFlushAndClose(link,0);
}

int test(char *ip,int port)
{
	NetLink		*link=0;
	U32			last_pkt_sent=0,last_bytes_sent=0,last_real_bytes=0;
	int			i,timer = timerAlloc(),count=0;
	char		str[10000] = "12 monkeys!\r\n\r\n";
	F32			dt;
	int			server = !port;
	char		buffer[] = "monkey";
	NetListen	*nl;

	EXCEPTION_HANDLER_BEGIN
	DO_AUTO_RUNS

	netSetSockBsd(1);
	comm = commCreate(server * 20,1);
	//commTimedDisconnect(comm,10);
	//commRandomDisconnect(comm,10000);

	if (server)
	{
		//nl = commListen(comm,LINK_HTTP,8080,httpMsg,netTestConnect,netTestDisconnect,sizeof(NetTestClient));
		nl = commListen(comm,LINKTYPE_UNSPEC, 0,8081,netTestMsg,netTestServerConnect,netTestDisconnect,sizeof(NetTestClient));
		for(;;)
		{
			commMonitor(comm);
#if 0
			if (g_link && linkConnected(g_link))
			{
				randTest(g_link);
				printf("flush and close\n");
				linkFlushAndClose(g_link,0);
				g_link = 0;
			}
#endif
		}
	}
	else
	{
        // randTest
		for(;;)
		{
			link = commConnectWait(comm,LINKTYPE_UNSPEC, 0,ip,port,netTestMsg,netTestClientConnect,netTestDisconnect,sizeof(NetTestClient),300000.0);
			if (link)
			{
#if 0
				linkSendRecvSize(link,5000,0);
				linkFlushLimit(link,1000);
#endif
				for(i=0; i<1111; i++)
				{
					linkFlush(link);
					commMonitor(comm);
					if (!linkConnected(link))
					{
						break;
					}
					printf("%d\n",linkSendBufFull(link));
					randTest(link);
					Sleep(1);
					//linkFlush(link);
					//commMonitor(comm);
					//Sleep(1);
					//commMonitor(comm);
					//linkFlushAndClose(link,0);
				}
				//printf("got disconnected!\n");
				linkRemove(&link);
                break;
			}
		}

        // dataTest
        if(0) {
		    link = commConnect(comm,LINKTYPE_UNSPEC, 0,ip,port,netTestMsg,netTestClientConnect,netTestDisconnect,0);
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
				    link = commConnect(comm,LINKTYPE_UNSPEC, 0,ip,port,netTestMsg,netTestClientConnect,netTestDisconnect,0);
				    linkFlushLimit(link,5000);
			    }
			    if (linkDisconnected(link))
			    {
				    printf("reconnecting..\n");
				    free(link);
				    link = commConnect(comm,LINKTYPE_UNSPEC, 0,ip,port,netTestMsg,netTestClientConnect,netTestDisconnect,0);
				    linkFlushLimit(link,5000);
				    //linkSetTimeout(link,5);
			    }
			    Sleep(0);
		    }
	    }
    
        printf("\ndone\n");
    }
	EXCEPTION_HANDLER_END
	return 0;
}

#if _XBOX
int wmain(int argc_in,char **argv_in)
#else
int wmain(int argc, WCHAR** argv_wide)
#endif
{
#if _XBOX
	int argc;
	char *argv[100];
	char *cmdline = GetCommandLine();
	argc = tokenize_line_quoted(cmdline, argv, NULL);
#endif

	if (argc == 2)
		test("127.0.0.1",atoi(argv[1]));
	else if (argc == 3)
		test(argv[1],atoi(argv[2]));
	else
		test(0,0);

    return 0;
}

#endif