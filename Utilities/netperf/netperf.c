#include <stdio.h>
#include <stdlib.h>
#ifdef WIN32
#include "windows.h"
#include <winsock.h>
#else
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#define closesocket(x) close(x)
#define ioctlsocket(x,y,z) ioctl(x,y,(unsigned long)z)
#define Sleep(x) usleep(x*1000)
#endif

int		send_size,recv_size,packet_size=1000;
char    net_addr[128] = "127.0.0.1";
int		ip_port = 6000;


void sockSetAddr(struct sockaddr_in *addr,unsigned int ip,int port)
{
    memset(addr,0,sizeof(struct sockaddr_in));
    addr->sin_family=AF_INET;
    addr->sin_addr.s_addr=ip;
    addr->sin_port = htons((u_short)port);
}

void sockBind(int sock,const struct sockaddr_in *name)
{
    if (bind (sock,(struct sockaddr *) name, sizeof(struct sockaddr_in)) >= 0) return;

	printf("cant bind socket!\n");
	exit(1);
}

void sockSetBlocking(int fd,int block)
{
	int  noblock;

    noblock = !block;
    ioctlsocket (fd, FIONBIO, &noblock);
}

void sockBufferSize(int socket,int send_size,int recv_size)
{
    if (send_size)
		setsockopt(socket,SOL_SOCKET,SO_SNDBUF,(void *)&send_size,sizeof(send_size));
    if (recv_size)
		setsockopt(socket,SOL_SOCKET,SO_RCVBUF,(void *)&recv_size,sizeof(recv_size));
}

int sockHostIp(char *s)
{
	char    buf[1000];
	struct hostent    *host_ent;

    gethostname(buf,sizeof(buf));
    host_ent = gethostbyname(buf);
     sprintf(s,"%d.%d.%d.%d",
        (unsigned char)host_ent->h_addr_list[0][0],
        (unsigned char)host_ent->h_addr_list[0][1],
        (unsigned char)host_ent->h_addr_list[0][2],
        (unsigned char)host_ent->h_addr_list[0][3]);
    if (strcmp(s,"127.0.0.1")==0)    // I always get the default localhost ip when I run this on linux
        return 0;                    // Works fine on NT. Anyone know why?
    return 1;
}

void    sockStart()
{
#ifdef WIN32
WORD wVersionRequested;
WSADATA wsaData;
int err;
wVersionRequested = MAKEWORD(2, 0);

    err = WSAStartup(wVersionRequested, &wsaData);

    if (err) fprintf(stderr,"Winsock error..");
#endif
}

#ifdef WIN32
typedef struct twolongs
{
    unsigned long high;
    unsigned long low;
};


unsigned long   timerTicks()
{
LARGE_INTEGER li;
struct twolongs *tl;

    QueryPerformanceCounter(&li);
    tl = (void *) & li;

     return tl->high;
}

unsigned long   timerSpeed()
{
static LARGE_INTEGER freq;
struct twolongs *tl;
static int init;

    if (!init)
    {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    tl = (void *) & freq;

     return tl->high;
}

#else

unsigned long   timerSpeed()
{
    return 1000000;
}

unsigned long   timerTicks()
{
	struct timeval  tv;

    gettimeofday(&tv,0);
    return (tv.tv_sec & 1023) * 1000000 + tv.tv_usec;
}

#endif

float timerElapsed(unsigned long start_time)
{
unsigned long    dt;

    dt = timerTicks() - start_time;
    return (float)dt / (float)timerSpeed();
}

void sock_select(int socket,int write_set)
{
	fd_set readSet;
	fd_set writeSet;

	FD_ZERO(&readSet);
	FD_ZERO(&writeSet);

	if (write_set)
		FD_SET(socket, &writeSet);
	else
		FD_SET(socket, &readSet);
#if 0
	if(!readSet.fd_count && !writeSet.fd_count)
		return;
#endif

	// Wait for an incoming packet or for the outgoing buffer to be freed up.
	{	
		struct timeval interval;
		int selectResult;

		interval.tv_sec = 0;
		interval.tv_usec = 0;	// Wait up to one millisecond for an incoming packet.

		if (0)
			selectResult = select(socket+1, &readSet, &writeSet, NULL, &interval);
		else
			selectResult = select(socket+1, &readSet, &writeSet, NULL, NULL);
	}
}

static char   data[1000000];

void stest()
{
	int		ret,addrlen,one=1;
	int		sock;
	struct sockaddr_in addr;
	int    amt,cnt=0,svr_sock;
	int    addr_len = sizeof(struct sockaddr_in);
	struct sockaddr_in addr_in;

    svr_sock = (int)socket(AF_INET,SOCK_STREAM,0);
    sockSetAddr(&addr_in,htonl(INADDR_ANY),ip_port);
    sockBind(svr_sock,&addr_in);
    sockBufferSize(svr_sock,send_size,recv_size);

	for(;;)
	{
		printf("Waiting for connection..\n");
		ret = listen(svr_sock,SOMAXCONN);
		if (ret)
		{
			printf("listen error..\n");
			return;
		}

		addrlen = sizeof(addr);
		sock = (int)accept(svr_sock,(struct sockaddr *)&addr,&addrlen);
		if (socket < 0)
			return;
		sockSetBlocking((int)sock,1);
		setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,(void *)&one,sizeof(one));

		printf("accepted.\n");
		for(;;)
		{
			amt = send(sock,data,packet_size,0);
			if (amt < 0)
			{
				closesocket(sock);
				break;
			}
		}
	}
}

void ctest()
{
	int		client_sock;
	struct  sockaddr_in addr;
	int     amt,ret,one=1;
	int     addr_len = sizeof(struct sockaddr_in),total_count=0,total_recv=0;
	float   dt=0;
	FILE    *file;
	unsigned long start_time=0;

    file = fopen("netperf.txt","wb");
    client_sock = (int)socket(AF_INET,SOCK_STREAM,0);
    sockSetAddr(&addr,inet_addr(net_addr),ip_port);
    sockBufferSize((int)client_sock,send_size,recv_size);

	ret = connect(client_sock,(struct sockaddr *)&addr,sizeof(addr));
	if (ret != 0)
	{
		closesocket(client_sock);
		return;
	}
    sockSetBlocking((int)client_sock,1);
	setsockopt(client_sock,IPPROTO_TCP,TCP_NODELAY,(void *)&one,sizeof(one));
	for(;;)
	{
		//sock_select(client_sock,0);
		amt = recv(client_sock,data,packet_size,0);
		if (amt < 0)
			exit(0);
		total_recv += amt;
		total_count++;
		if (timerElapsed(start_time) >= 1.f)
		{
			printf("1 second: %d recvd (%d packets)\n",total_recv,total_count);
			start_time = timerTicks();
			total_count=0;
			total_recv=0;
		}
	}
}

int main(int argc,char **argv)
{
	char    buf[1000];
	int		i;

    if (argc==1)
    {
        printf("Usage: netperf -s  -c <ip address> [options]\n");
        printf("   ie: netperf -s (run as server)\n");
        printf("   ie: netperf -c 127.0.0.1 (run as client, connect to localhost)\n");
        printf("   options:\n");
        printf("    -send <tcp send buffer size>:\n");
        printf("    -recv <tcp receive buffer size>:\n");
        printf("    -packetsize <size of packet to send>:\n");
        printf("    -port <ip port to use>:\n");
        exit(0);
    }
    sockStart();
    if (sockHostIp(buf))
        printf("Local IP: %s\n",buf);
    if (argc > 2)
        strcpy(net_addr,argv[2]);
	for(i=2;i<argc;i++)
	{
		if (strcmp(argv[i],"-send")==0)
			send_size = atoi(argv[i+1]);
		if (strcmp(argv[i],"-recv")==0)
			recv_size = atoi(argv[i+1]);
		if (strcmp(argv[i],"-packetsize")==0)
			packet_size = atoi(argv[i+1]);
		if (strcmp(argv[i],"-port")==0)
			ip_port = atoi(argv[i+1]);
	}
	printf("packet size     : %d\n",packet_size);
	printf("send buffer size: %d\n",send_size);
	printf("recv buffer size: %d\n",recv_size);
	printf("port            : %d\n",ip_port);
    if (strcmp(argv[1],"-s")==0)
        stest();
    if (strcmp(argv[1],"-c")==0)
        ctest();
    return 0;
}
